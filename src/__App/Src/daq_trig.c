/*
 * CTU/PillScope project
 * Author: Jakub Parez <parez.jakub@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cfg.h"
#include "daq.h"
#include "daq_trig.h"
#include "utility.h"
#include "periph.h"
#include "comm.h"
#include "main.h"


void daq_trig_init(daq_data_t* self)
{
    self->trig.ignore = 0;
    self->trig.ready = 0;
    self->trig.cntr = 0;
    self->trig.ch_reg = 0;
    self->trig.all_cntr = 0;
    self->trig.pos_frst = 0;
    self->trig.pos_trig = 0;
    self->trig.pos_last = 0;
    self->trig.pos_diff = 0;
    self->trig.uwtick_first = 0;
    self->trig.pretrig_cntr = 0;
    self->trig.is_post = 0;
    self->trig.posttrig_size = 0;
    self->trig.auttrig_val = 0;
    self->trig.pretrig_val = 0;
    self->trig.fullmem_val = 0;
    self->trig.buff_trig = NULL;
    self->trig.dma_trig = PS_DMA_CH_ADC1;
    self->trig.exti_trig = PS_LA_IRQ_EXTI1;
    self->trig.order = 0;
    self->trig.ready_last = 0;
    self->trig.post_start = 0;
    self->trig.post_from = 0;
}

void daq_trig_check(daq_data_t* self)
{
    if (self->enabled) //&& self->trig.is_post == 0 && self->trig.ready == 0)
    {
        if (uwTick >= self->trig.uwtick_first)
            self->trig.pretrig_cntr = uwTick - self->trig.uwtick_first;
        else
            self->trig.pretrig_cntr = (uwTick - self->trig.uwtick_first) + 4294967295;
    }
    else
    {
        self->trig.pretrig_cntr = 0;
    }


    if (self->mode != VM) // SCOPE || LA
    {
        if (self->enabled &&
            self->trig.set.mode == AUTO &&
            self->trig.is_post == 0 &&
            self->trig.ready == 0 &&
            self->trig.pretrig_cntr > self->trig.auttrig_val)
{
            daq_enable(self, 0);
            self->trig.pos_frst = PS_DMA_LAST_IDX(self->trig.buff_trig->len, PS_DMA_CH_ADC1);

            self->trig.ready = 1;
            self->trig.is_post = 0;

            comm_respond(comm_ptr, PS_RESP_RDY, 9);
        }
        else if (self->trig.set.mode == DISABLED &&
                 self->trig.pretrig_cntr > self->trig.fullmem_val)
        {
            self->trig.ready = 1;
            if (self->trig.ready_last == 0)
                comm_respond(comm_ptr, PS_RESP_RDY, 9);
        }
    }
    self->trig.ready_last = self->trig.ready;
}

void daq_trig_trigger_scope(daq_data_t* self)
{
    ASSERT(self->trig.buff_trig != NULL);
    ASSERT(self->trig.dma_trig != 0);

    int last_idx = PS_DMA_LAST_IDX(self->trig.buff_trig->len, self->trig.dma_trig);

    if (self->trig.ready || self->trig.post_start)
        return;

    int prev_last_idx = last_idx - self->trig.buff_trig->chans;
    if (prev_last_idx < 0)
        prev_last_idx += self->trig.buff_trig->len;

    uint16_t last_val = 0;
    uint16_t prev_last_val = 0;

    if (self->set.bits == B8)
    {
        last_val = (uint16_t)(((uint8_t*)(self->trig.buff_trig->data))[last_idx]);
        prev_last_val = (uint16_t)(((uint8_t*)(self->trig.buff_trig->data))[prev_last_idx]);
    }
    else
    {
        last_val = (*((uint16_t*)(((uint8_t*)self->trig.buff_trig->data)+(last_idx*2))));
        prev_last_val = (*((uint16_t*)(((uint8_t*)self->trig.buff_trig->data)+(prev_last_idx*2))));
    }


    /*
    if (tignore)
    {
        tignore = 0;

        uint32_t h = LL_ADC_GetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_HIGH);
        uint32_t l = LL_ADC_GetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_LOW);

        LL_ADC_SetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_HIGH, l);
        LL_ADC_SetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_LOW, h);
    }
    else
    {
    */
        // trigger condition
        if ((self->trig.set.edge == RISING && last_val > self->trig.set.val && prev_last_val <= self->trig.set.val) ||
            (self->trig.set.edge == FALLING && last_val < self->trig.set.val && prev_last_val >= self->trig.set.val))
        {
            if (self->trig.pretrig_cntr > self->trig.pretrig_val) // pretrigger counter
            {
                //self->trig.pretrig_cntr = 0;
                daq_trig_poststart(self, last_idx);
            }
        }
        self->trig.all_cntr++;

        /*
        //else // false trig, switch edges and wait for another window
        //{
            tignore = 1;

            uint32_t h = LL_ADC_GetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_HIGH);
            uint32_t l = LL_ADC_GetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_LOW);

            LL_ADC_SetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_HIGH, l);
            LL_ADC_SetAnalogWDThresholds(ADC1, LL_ADC_AWD_THRESHOLD_LOW, h);
            trig_false_cntr++;
        //}
         */
    //}
}

void daq_trig_trigger_la(daq_data_t* self)
{
    ASSERT(self->trig.buff_trig != NULL);
    ASSERT(self->trig.dma_trig != 0);

    int last_idx = PS_DMA_LAST_IDX(self->trig.buff_trig->len, self->trig.dma_trig);

    if (self->trig.ready || self->trig.post_start)
        return;

    if (self->trig.pretrig_cntr > self->trig.pretrig_val)
    {
        self->trig.pretrig_cntr = 0;
        daq_trig_poststart(self, last_idx);
    }
}

void daq_trig_poststart(daq_data_t* self, int pos)
{
    self->trig.post_start = 1;
    self->trig.post_from = pos;
    //self->trig.trig_data_last_idx = last_idx;
}
void daq_trig_postcount(daq_data_t* self)
{
    int last_idx = self->trig.post_from;

    ASSERT(self->trig.buff_trig != NULL);

    self->trig.is_post = 1;
    self->trig.cntr++;

    self->trig.pos_trig = last_idx + self->trig.order;
    if (self->trig.pos_trig >= self->trig.buff_trig->len)
        self->trig.pos_trig -= self->trig.buff_trig->len;

    int post = (int)((float)self->set.mem * ((float)(100 - self->trig.set.pretrigger) / 100.0));
    self->trig.posttrig_size = post * self->trig.buff_trig->chans;

    self->trig.pos_last = self->trig.pos_trig + self->trig.posttrig_size;
    if (self->trig.pos_last >= self->trig.buff_trig->len)
        self->trig.pos_last -= self->trig.buff_trig->len;

    self->trig.pos_frst = self->trig.pos_trig - ((self->set.mem - post + 1) * self->trig.buff_trig->chans) + 1;
    if (self->trig.pos_frst >= self->trig.buff_trig->len)
        self->trig.pos_frst -= self->trig.buff_trig->len;
    if (self->trig.pos_frst < 0)
        self->trig.pos_frst += self->trig.buff_trig->len;


    if (self->mode != LA)
    {
#if defined(PS_ADC_MODE_ADC1) || defined(PS_ADC_MODE_ADC12) || defined(PS_ADC_MODE_ADC1234)
        LL_ADC_SetAnalogWDMonitChannels(ADC1, LL_ADC_AWD_DISABLE);
#endif

#if defined(PS_ADC_MODE_ADC12) || defined(PS_ADC_MODE_ADC1234)
        LL_ADC_SetAnalogWDMonitChannels(ADC2, LL_ADC_AWD_DISABLE);
#endif

#if defined(PS_ADC_MODE_ADC1234)
        LL_ADC_SetAnalogWDMonitChannels(ADC3, LL_ADC_AWD_DISABLE);
        LL_ADC_SetAnalogWDMonitChannels(ADC4, LL_ADC_AWD_DISABLE);
#endif
    }
    else
    {
        NVIC_DisableIRQ(self->trig.exti_trig);
    }

    self->trig.pretrig_cntr = 0;

    while(1)
    {
        iwdg_feed();
        int last_idx = PS_DMA_LAST_IDX(self->trig.buff_trig->len, self->trig.dma_trig);

        self->trig.pos_diff = self->trig.pos_last - self->trig.pos_trig;

        if (self->trig.pos_diff < 0)
            self->trig.pos_diff += self->trig.buff_trig->len;

        if (self->trig.pos_last == last_idx)
        {
            daq_enable(self, 0);
            self->trig.ready = 1;
            self->trig.is_post = 0;
            comm_respond(comm_ptr, PS_RESP_RDY, 9); // data ready

            break;
        }
    }
    self->trig.post_start = 0;
}

void daq_trig_update(daq_data_t* self)
{
    daq_trig_set(self, self->trig.set.ch, self->trig.set.val_percent,
                 self->trig.set.edge, self->trig.set.mode, self->trig.set.pretrigger);
}

void daq_trig_disable(daq_data_t* self)
{
    daq_trig_set(self, 0, self->trig.set.val_percent, self->trig.set.edge, DISABLED, self->trig.set.pretrigger);
}

int daq_trig_set(daq_data_t* self, uint32_t ch, uint8_t level, enum trig_edge edge, enum trig_mode mode, int pretrigger)
{
    if((level < 0 || level > 100) ||
       (ch < 0 || ch > 4) ||
       (pretrigger > 99 || pretrigger < 1))
    {
        return -1;
    }

    daq_enable(self, 0);
    daq_reset(self);

#if defined(PS_ADC_MODE_ADC1)

    ADC_TypeDef* adc = ADC1;
    self->trig.buff_trig = &self->buff1;
    self->trig.dma_trig = PS_DMA_CH_ADC1;
    int ch_cnt = self->set.ch1_en + self->set.ch2_en + self->set.ch3_en + self->set.ch4_en + 1;
    int it = 1;
    if (self->set.ch1_en){
        it++;
        if (ch == 1) self->trig.order = ch_cnt - it;
    }
    if (self->set.ch2_en){
        it++;
        if (ch == 2) self->trig.order = ch_cnt - it;
    }
    if (self->set.ch3_en){
        it++;
        if (ch == 3) self->trig.order = ch_cnt - it;
    }
    if (self->set.ch4_en){
        it++;
        if (ch == 4) self->trig.order = ch_cnt - it;
    }

#elif defined(PS_ADC_MODE_ADC12)

    uint32_t adc;
    if (ch == 1 || ch == 2)
    {
        adc = ADC1;
        self->trig.buff_trig = &self->buff1;
        self->trig.dma_trig = PS_DMA_CH_ADC1;

        int ch_cnt = self->set.ch1_en + self->set.ch2_en + 1;
        int it = 1;
        if (self->set.ch1_en){
            it++;
            if (ch == 1) self->trig.order = ch_cnt - it;
        }
        if (self->set.ch2_en){
            it++;
            if (ch == 2) self->trig.order = ch_cnt - it;
        }
    }
    else // if (ch == 3 || ch == 4)
    {
        adc = ADC2;
        self->trig.buff_trig = &self->buff2;
        self->trig.dma_trig = PS_DMA_CH_ADC2;

        int ch_cnt = self->set.ch3_en + self->set.ch4_en;
        int it = 0;
        if (self->set.ch3_en){
            it++;
            if (ch == 3) self->trig.order = ch_cnt - it;
        }
        if (self->set.ch4_en){
            it++;
            if (ch == 4) self->trig.order = ch_cnt - it;
    }

#elif defined(PS_ADC_MODE_ADC1234)

    uint32_t adc;
    self->trig.order = 0;
    if (ch == 1)
    {
        adc = ADC1;
        self->trig.buff_trig = &self->buff1;
        self->trig.dma_trig = PS_DMA_CH_ADC1;
    }
    if (ch == 2)
    {
        adc = ADC2;
        self->trig.buff_trig = &self->buff2;
        self->trig.dma_trig = PS_DMA_CH_ADC2;
    }
    else if (ch == 3)
    {
        adc = ADC3;
        self->trig.buff_trig = &self->buff3;
        self->trig.dma_trig = PS_DMA_CH_ADC3;
    }
    else // if (ch == 4)
    {
        adc = ADC4;
        self->trig.buff_trig = &self->buff4;
        self->trig.dma_trig = PS_DMA_CH_ADC4;
    }

#endif

    self->trig.fullmem_val = (int)(((1.0 / (float)self->set.fs) * (float)self->set.mem) * (float)PS_SYSTICK_FREQ) + 1;
    if (self->trig.pretrig_val < 10)
        self->trig.pretrig_val = 10;

    self->trig.auttrig_val = PS_AUTRIG_MIN_MS + (int)((float)self->trig.fullmem_val * 1.0);

    if (ch == 0 || mode == DISABLED)
    {
        ASSERT(self->trig.exti_trig != 0);

        NVIC_DisableIRQ(self->trig.exti_trig);
        LL_ADC_SetAnalogWDMonitChannels(adc, LL_ADC_AWD_DISABLE);

        self->trig.set.ch = 0;
        self->trig.set.mode = DISABLED;

        daq_enable(self, 1);
        return 0;
    }

    if (self->mode == LA)
    {
        self->trig.buff_trig = &self->buff1;
        self->trig.dma_trig = PS_DMA_CH_LA;

        LL_ADC_SetAnalogWDMonitChannels(adc, LL_ADC_AWD_DISABLE);

        LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
        uint32_t extiline = 0;
        uint32_t exti = 0;

        if (self->set.ch1_en)
        {
            self->trig.exti_trig = PS_LA_IRQ_EXTI1;
            extiline = PS_LA_EXTILINE1;
            exti = PS_LA_EXTI1;
        }
        else if (self->set.ch2_en)
        {
            self->trig.exti_trig = PS_LA_IRQ_EXTI2;
            extiline = PS_LA_EXTILINE2;
            exti = PS_LA_EXTI2;
        }
        else if (self->set.ch3_en)
        {
            self->trig.exti_trig = PS_LA_IRQ_EXTI3;
            extiline = PS_LA_EXTILINE3;
            exti = PS_LA_EXTI3;
        }
        else // if (self->set.ch4_en)
        {
            self->trig.exti_trig = PS_LA_IRQ_EXTI4;
            extiline = PS_LA_EXTILINE4;
            exti = PS_LA_EXTI4;
        }

        LL_GPIO_AF_SetEXTISource(PS_LA_EXTI_PORT, extiline);

        EXTI_InitStruct.Line_0_31 = exti;
        EXTI_InitStruct.LineCommand = ENABLE;
        EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
        EXTI_InitStruct.Trigger = (self->trig.set.edge == RISING ? LL_EXTI_TRIGGER_RISING : LL_EXTI_TRIGGER_FALLING);
        LL_EXTI_Init(&EXTI_InitStruct);

        NVIC_SetPriority(self->trig.exti_trig, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
        NVIC_EnableIRQ(self->trig.exti_trig);

        self->trig.set.val = 0;
        self->trig.set.val_percent = 0;
    }
    else // SCOPE
    {
        ASSERT(self->trig.exti_trig != 0);
        NVIC_DisableIRQ(self->trig.exti_trig);

        if ((ch == 1 && self->set.ch1_en) ||
            (ch == 2 && self->set.ch2_en) ||
            (ch == 3 && self->set.ch3_en) ||
            (ch == 4 && self->set.ch4_en))
        {
            if (ch == 1)
                self->trig.ch_reg = PS_ADC_AWD1;
            else if (ch == 2)
                self->trig.ch_reg = PS_ADC_AWD2;
            else if (ch == 3)
                self->trig.ch_reg = PS_ADC_AWD3;
            else if (ch == 4)
                self->trig.ch_reg = PS_ADC_AWD4;

            LL_ADC_SetAnalogWDMonitChannels(adc, self->trig.ch_reg);

            uint32_t level_raw = (int)(self->adc_max_val / 100.0 * (float)level);

            if (edge == RISING)
            {
                LL_ADC_SetAnalogWDThresholds(adc, LL_ADC_AWD_THRESHOLD_HIGH, level);
                LL_ADC_SetAnalogWDThresholds(adc, LL_ADC_AWD_THRESHOLD_LOW, 0);
            }
            else // (edge == FALLING)
            {
                LL_ADC_SetAnalogWDThresholds(adc, LL_ADC_AWD_THRESHOLD_HIGH, (int)self->adc_max_val);
                LL_ADC_SetAnalogWDThresholds(adc, LL_ADC_AWD_THRESHOLD_LOW, level);
            }

            self->trig.set.val = level_raw;
            self->trig.set.val_percent = level;
        }
        else return -1;

        self->trig.pretrig_val = (int)((float)self->trig.fullmem_val * ((float)pretrigger / 100.0)) + 1;
        self->trig.set.pretrigger = pretrigger;
        self->trig.set.mode = mode;
        self->trig.set.edge = edge;
        self->trig.set.ch = ch;
    }


    daq_enable(self, 1);
    return 0;
}
