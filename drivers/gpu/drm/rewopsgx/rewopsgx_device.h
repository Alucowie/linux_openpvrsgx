/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2025 Anthoine Bourgeois <anthoine.bourgeois@gmail.com> */

#ifndef __REWOPSGX_DEVICE_H__
#define __REWOPSGX_DEVICE_H__

/*
 * Features that cannot be automatically detected and need matching using the
 * compatible string, typically SoC-specific.
 */
struct rewopsgx_compatible {
	u16 core_id_register_nr;
};

#endif
