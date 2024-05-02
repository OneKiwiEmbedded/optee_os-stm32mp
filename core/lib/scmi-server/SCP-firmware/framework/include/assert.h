/*
 * Arm SCP/MCP Software
 * Copyright (c) 2020-2021, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ASSERT_H
#define ASSERT_H

#include_next <assert.h> /* IWYU pragma: keep */

#ifndef static_assert
#    define static_assert _Static_assert
#endif

#endif /* ASSERT_H */
