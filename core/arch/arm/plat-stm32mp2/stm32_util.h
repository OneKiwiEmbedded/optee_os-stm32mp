/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022, STMicroelectronics
 */

#ifndef __STM32_UTIL_H__
#define __STM32_UTIL_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <types_ext.h>

static inline void stm32mp_register_secure_periph_iomem(vaddr_t base __unused)
{
}

static inline void stm32mp_register_non_secure_periph_iomem(vaddr_t base
							    __unused) { }

/*
 * Generic spinlock function that bypass spinlock if MMU is disabled or
 * lock is NULL.
 */
uint32_t may_spin_lock(unsigned int *lock);
void may_spin_unlock(unsigned int *lock, uint32_t exceptions);
#endif /*__STM32_UTIL_H__*/
