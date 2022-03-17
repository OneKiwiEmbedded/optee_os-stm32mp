/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022, STMicroelectronics
 */

#ifndef __STM32_SYSCONF_H__
#define __STM32_SYSCONF_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <types_ext.h>

/* syscon banks */
enum syscon_banks {
	SYSCON_A35SSC,
	SYSCON_NB_BANKS
};

#define SYSCON_ID(bank, offset) (((bank) << 16) | \
				((offset) & GENMASK_32(15, 0)))

/*
 * A35SSC register offsets (base relative)
 */
#define A35SSC_M33CFG_ACCESS_CR  SYSCON_ID(SYSCON_A35SSC, 0x2088U)
#define A35SSC_M33_TZEN_CR       SYSCON_ID(SYSCON_A35SSC, 0x20A0U)
#define A35SSC_M33_INITSVTOR_CR  SYSCON_ID(SYSCON_A35SSC, 0x20A4U)
#define A35SSC_M33_INITNSVTOR_CR SYSCON_ID(SYSCON_A35SSC, 0x20A8U)

/*
 * A35SSC M33CFG_ACCESS_CR register
 */
#define A35SSC_M33_TZEN_CR_M33CFG_SEC          BIT(0)
#define A35SSC_M33_TZEN_CR_M33CFG_PRIV         BIT(1)

/*
 * A35SSC M33_TZEN_CR register
 */
#define A35SSC_M33_TZEN_CR_CFG_SECEXT          BIT(0)

void stm32mp_syscfg_write(uint32_t id, uint32_t value, uint32_t bitmsk);
uint32_t stm32mp_syscfg_read(uint32_t id);

#endif /*__STM32_SYSCONF_H__*/
