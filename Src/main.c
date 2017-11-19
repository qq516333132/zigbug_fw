/**
 * Copyright (c) 2014 - 2017, Nordic Semiconductor ASA
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 * 
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 * 
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 * 
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 * 
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */
/** @file
 *
 * @defgroup blinky_example_main main.c
 * @{
 * @ingroup blinky_example
 * @brief Blinky Example Application main file.
 *
 * This file contains the source code for a sample application to blink LEDs.
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
//#include "boards.h"
#include "neopixels.h"
#include "audio.h"
#include "mic.h"
#include "batt_meas.h"
#include "app_error.h"
#include "app_timer.h"
#include "app_scheduler.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_clock.h"
#include "vision.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#define LOCAL_DEBUG
#include "debug.h"

#define DEAD_BEEF   0xDEADBEEF          /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */
#define SCHED_MAX_EVENT_DATA_SIZE   MAX(APP_TIMER_SCHED_EVENT_DATA_SIZE, 0) 
#define SCHED_QUEUE_SIZE            60  /**< Maximum number of events in the scheduler queue. */

static rgb_t       m_led_rgb_body[4];
const led_group_t m_body_leds = 
{
	.out_pin = 14,
	.led_num = 4,
	.rgb_buf = (rgb_t*)&m_led_rgb_body,
};
rgb_t red = {255, 0, 0};
rgb_t green = {0, 255, 0};
rgb_t blue = {0, 0, 255};
rgb_t white = {255, 255, 255};
rgb_t black = {0, 0, 0};

static const nrf_drv_spi_t neopixels_spi_instance = NRF_DRV_SPI_INSTANCE(0);
#include "wav_1.h"
static uint16_t listen_buffer[26000];

#define SPEED 20

uint32_t error_code;
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    #ifdef DEBUG
    app_error_print(id, pc, info);
    #endif
    error_code = ((error_info_t *)(info))->err_code;
    UNUSED_VARIABLE(error_code);
    while (1)
    {
       neopixels_write_rgb(&m_body_leds, 0, &red);
       nrf_delay_ms(200);
       neopixels_write_rgb(&m_body_leds, 0, &green);
       nrf_delay_ms(200);
    };
}

static void lfclk_config(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);

    nrf_drv_clock_lfclk_request(NULL);
}
#define TASK_DELAY        200           /**< Task delay. Delays a LED0 task for 200 ms */
#define TIMER_PERIOD      1000          /**< Timer period. LED1 timer will expire after 1000 ms */

TaskHandle_t  led_toggle_task_handle;   /**< Reference to LED0 toggling FreeRTOS task. */
TaskHandle_t  vision_task_handle;   /**< Reference to LED0 toggling FreeRTOS task. */
TimerHandle_t led_toggle_timer_handle;  /**< Reference to LED1 toggling FreeRTOS timer. */

/**@brief LED0 task entry function.
 *
 * @param[in] pvParameter   Pointer that will be used as the parameter for the task.
 */
static void led_toggle_task_function (void * pvParameter)
{
    UNUSED_PARAMETER(pvParameter);
    while (true)
    {
        neopixels_write_rgb(&m_body_leds, 0, &red);

        /* Delay a task for a given number of ticks */
        vTaskDelay(50);
        neopixels_write_rgb(&m_body_leds, 0, &black);
        vTaskDelay(1500);

        /* Tasks must be implemented to never return... */
    }
}

APP_TIMER_DEF(breath_led_app_timer_id);
bool led_breath_toggle = false;
static void app_timer_breath_led_handler(void * unused)
{
    if(led_breath_toggle)
    {
        neopixels_write_rgb(&m_body_leds, 0, &red);
        //motor_start(-SPEED, -SPEED);
    }
    else
    {
        neopixels_write_rgb(&m_body_leds, 0, &black);
        //motor_start(SPEED, SPEED);
    }
    led_breath_toggle = !led_breath_toggle;
}
void enable_breath_led(uint32_t meas_interval_ms)
{
    uint32_t err_code = app_timer_create(&breath_led_app_timer_id,
        APP_TIMER_MODE_REPEATED,
        app_timer_breath_led_handler);
    RETURN_IF_ERROR(err_code);

    err_code = app_timer_start(breath_led_app_timer_id,
                                APP_TIMER_TICKS(meas_interval_ms), NULL);
    RETURN_IF_ERROR(err_code);
}

/**@brief LED0 task entry function.
 *
 * @param[in] pvParameter   Pointer that will be used as the parameter for the task.
 */
static void vision_task_function(void *pvParameter)
{
    UNUSED_PARAMETER(pvParameter);
    vision_init();
    while (true)
    {
        vision_start_measure(VISION_LEFT_EYE);
        vTaskDelay(10);
        int16_t dist = vision_get_measure(VISION_LEFT_EYE);
        if (dist <= 150)
        {
            motor_start(-SPEED, SPEED);
        }
        else
        {
            motor_start(SPEED, SPEED);
        }
    }
}

/**
 * @brief Function for application main entry.
 */
int main(void)
 {
    lfclk_config();
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
    //app_timer_init();
    /* Configure board. */
    //bsp_board_leds_init();
    nrf_drv_gpiote_init();
    audio_init();
    mic_init();
    //vision_init();
    motor_init();
    //batt_meas_init(NULL);
    //batt_meas_enable(5000);
    //motor_start(-SPEED, -SPEED);
    neopixel_init(&m_body_leds, 1, &neopixels_spi_instance);
    //enable_breath_led(1000);
    /* Toggle LEDs. */
    /* Create task for LED0 blinking with priority set to 2 */
    UNUSED_VARIABLE(xTaskCreate(led_toggle_task_function, "LED0", configMINIMAL_STACK_SIZE + 200, NULL, 2, &led_toggle_task_handle));

    UNUSED_VARIABLE(xTaskCreate(vision_task_function, "VISION", configMINIMAL_STACK_SIZE + 200, NULL, 3, &vision_task_handle));
    
    /* Activate deep sleep mode */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    /* Start FreeRTOS scheduler. */
    vTaskStartScheduler();

    while (true)
    {
        //audio_play(DATA, SOUND_LENGTH);
        //DEBUG_PRINTF(0, "Battery:%d%%\n", batt_meas_get_level());
       //mic_listen(0, listen_buffer, 2000);
       app_sched_execute();
    }
}

/**
 *@}
 **/
