/*
 * Copyright (C) 2016 Unwired Devices [info@unwds.com]
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file		umdk-lps331.c
 * @brief       umdk-lps331 module implementation
 * @author      Mikhail Churikov
 * @author		Evgeniy Ponomarev 
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "periph/gpio.h"
#include "periph/i2c.h"

#include "board.h"

#include "lps331ap.h"

#include "unwds-common.h"
#include "umdk-lps331.h"
#include "unwds-gpio.h"

#include "thread.h"
#include "xtimer.h"
#include "rtctimers.h"

static lps331ap_t dev;

static uwnds_cb_t *callback;

static kernel_pid_t timer_pid;

static msg_t timer_msg = {
};
static rtctimer_t timer;

static struct {
	uint8_t is_valid;
	uint8_t publish_period_min;
	uint8_t i2c_dev;
} lps331_config;

static bool is_polled = false;

static bool init_sensor(void)
{
    dev.i2c = UMDK_LPS331_I2C;

    return lps331ap_init(&dev, dev.i2c, 0x5D, LPS331AP_RATE_1HZ);
}

static void prepare_result(module_data_t *buf)
{
    int temperature_mc, pressure_mbar;
	int16_t temperature_unwds;

    temperature_mc = lps331ap_read_temp(&dev);
	temperature_unwds = (temperature_mc + 50)/100;
	
    pressure_mbar = lps331ap_read_pres(&dev);
	printf("[lps331] T: %d.%d C, P: %d mbar\n", temperature_unwds/10, abs(temperature_unwds%10), pressure_mbar);

     /* One byte for module ID, two bytes for temperature, two bytes for pressure*/
    buf->length = 1 + sizeof(temperature_unwds) + sizeof(pressure_mbar);

    buf->data[0] = UNWDS_LPS331_MODULE_ID;

    /* Copy measurements into response */
    memcpy(buf->data + 1, &temperature_unwds, sizeof(temperature_unwds));
    memcpy(buf->data + 1 + sizeof(temperature_unwds), &pressure_mbar, sizeof(pressure_mbar));
}

static void *timer_thread(void *arg)
{
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);

    puts("[umdk-lps331] Periodic publisher thread started");

    while (1) {
        msg_receive(&msg);

        rtctimers_remove(&timer);

        module_data_t data = {};
        data.as_ack = is_polled;
        is_polled = false;

        prepare_result(&data);

        /* Notify the application */
        callback(&data);

        /* Restart after delay */
        rtctimers_set_msg(&timer, 60 * lps331_config.publish_period_min, &timer_msg, timer_pid);
    }

    return NULL;
}

static void reset_config(void) {
	lps331_config.is_valid = 0;
	lps331_config.publish_period_min = UMDK_LPS331_PUBLISH_PERIOD_MIN;
	lps331_config.i2c_dev = UMDK_LPS331_I2C;
}

static void init_config(void) {
	reset_config();

	if (!unwds_read_nvram_config(UNWDS_LPS331_MODULE_ID, (uint8_t *) &lps331_config, sizeof(lps331_config)))
		return;

	if ((lps331_config.is_valid == 0xFF) || (lps331_config.is_valid == 0))  {
		reset_config();
		return;
	}

	if (lps331_config.i2c_dev >= I2C_NUMOF) {
		reset_config();
		return;
	}
}

static inline void save_config(void) {
	lps331_config.is_valid = 1;
	unwds_write_nvram_config(UNWDS_LPS331_MODULE_ID, (uint8_t *) &lps331_config, sizeof(lps331_config));
}

static void set_period (int period) {
    rtctimers_remove(&timer);

    lps331_config.publish_period_min = period;
	save_config();

    /* Don't restart timer if new period is zero */
    if (lps331_config.publish_period_min) {
        rtctimers_set_msg(&timer, 60 * lps331_config.publish_period_min, &timer_msg, timer_pid);
        printf("[umdk-lps331] Period set to %d seconds\n", lps331_config.publish_period_min);
    } else {
        puts("[umdk-lps331] Timer stopped");
    }
}

int umdk_lps331_shell_cmd(int argc, char **argv) {
    if (argc == 1) {
        puts ("lps331 get - get results now");
        puts ("lps331 send - get and send results now");
        puts ("lps331 period <N> - set period to N minutes");
        puts ("lps331 reset - reset settings to default");
        return 0;
    }
    
    char *cmd = argv[1];
	
    if (strcmp(cmd, "get") == 0) {
        module_data_t data = {};
        prepare_result(&data);
    }
    
    if (strcmp(cmd, "send") == 0) {
        is_polled = true;
		/* Send signal to publisher thread */
		msg_send(&timer_msg, timer_pid);
    }
    
    if (strcmp(cmd, "period") == 0) {
        char *val = argv[2];
        set_period(atoi(val));
    }
    
    if (strcmp(cmd, "reset") == 0) {
        reset_config();
        save_config();
    }
    
    return 1;
}

void umdk_lps331_init(uint32_t *non_gpio_pin_map, uwnds_cb_t *event_callback)
{
    (void) non_gpio_pin_map;

    callback = event_callback;
	
	init_config();
	printf("[lps331] Publish period: %d min\n", lps331_config.publish_period_min);

    if (init_sensor()) {
        puts("[umdk-lps331] Unable to init sensor!");
    }
	
    if (lps331ap_enable(&dev) < 1) {
        puts("[umdk-lps331] Unable to enable sensor!");
    }

    /* Create handler thread */
    char *stack = (char *) allocate_stack();
    if (!stack) {
    	puts("umdk-lps331: unable to allocate memory. Are too many modules enabled?");
    	return;
    }
    
    shell_command_t command = {"lps331", "type 'lps331' for commands list", umdk_lps331_shell_cmd};
    unwds_add_shell_command(command);

    timer_pid = thread_create(stack, UNWDS_STACK_SIZE_BYTES, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, timer_thread, NULL, "lps331ap thread");

    /* Start publishing timer */
    rtctimers_set_msg(&timer, 60 * lps331_config.publish_period_min, &timer_msg, timer_pid);
}

static void reply_fail(module_data_t *reply) {
	reply->length = 2;
	reply->data[0] = UNWDS_LPS331_MODULE_ID;
	reply->data[1] = 255;
}

static void reply_ok(module_data_t *reply) {
	reply->length = 2;
	reply->data[0] = UNWDS_LPS331_MODULE_ID;
	reply->data[1] = 0;
}

bool umdk_lps331_cmd(module_data_t *cmd, module_data_t *reply)
{
    if (cmd->length < 1) {
    	reply_fail(reply);
        return true;
    }

    umdk_lps331_cmd_t c = cmd->data[0];
    switch (c) {
        case UMDK_LPS331_CMD_SET_PERIOD: {
            if (cmd->length != 2) {
            	reply_fail(reply);
                break;
            }

            uint8_t period = cmd->data[1];
            set_period(period);

            reply_ok(reply);
            break;
        }

        case UMDK_LPS331_CMD_POLL:
        	is_polled = true;

            /* Send signal to publisher thread */
            msg_send(&timer_msg, timer_pid);

            return false; /* Don't reply */

            break;

        case UMDK_LPS331_CMD_SET_I2C: {
            i2c_t i2c = (i2c_t) cmd->data[1];
            dev.i2c = i2c;
			lps331_config.i2c_dev = i2c;

            init_sensor();

            reply_ok(reply);
            break;
        }

        default:
        	reply_fail(reply);
            break;
    }

    return true;
}

#ifdef __cplusplus
}
#endif
