/*
 * Copyright (C) 2017   Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    boards_nucleo32-f303 Nucleo32-F303
 * @ingroup     boards
 * @brief       Board specific files for the nucleo32-f303 board
 * @{
 *
 * @file
 * @brief       Board specific definitions for the nucleo32-f303 board
 *
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 */

#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Macros for controlling the on-board LED.
 * @{
 */
#define LED0_PIN            GPIO_PIN(PORT_B, 3)

#define LED0_MASK           (1 << 3)

#define LED0_ON             (GPIOB->BSRR    = LED0_MASK)
#define LED0_OFF            (GPIOB->BSRR    = (LED0_MASK << 16))
#define LED0_TOGGLE         (GPIOB->ODR     ^= LED0_MASK)
/** @} */

/**
 * @brief   Initialize board specific hardware, including clock, LEDs and std-IO
 */
void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
/** @} */