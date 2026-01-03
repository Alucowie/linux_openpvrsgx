// SPDX-License-Identifier: GPL-2.0
/* Copyright 2025 Anthoine Bourgeois <anthoine.bourgeois@gmail.com> */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "rewopsgx_device.h"
#include "rewopsgx_regs.h"

static int rewopsgx_probe(struct platform_device *pdev)
{
	printk(KERN_INFO "%s\n", __func__);
	return 0;
}

static void rewopsgx_remove(struct platform_device *pdev)
{
	printk(KERN_INFO "%s\n", __func__);
}

static const struct rewopsgx_compatible sgx520_data = {
	.core_id_register_nr = SGX520_CORE_ID,
};

static const struct rewopsgx_compatible sgx531_data = {
	.core_id_register_nr = SGX531_CORE_ID,
};

static const struct rewopsgx_compatible sgx545_data = {
	.core_id_register_nr = SGX545_CORE_ID,
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "img,powervr-sgx520", .data = &sgx520_data, },
	{ .compatible = "img,powervr-sgx530", .data = &sgx520_data, },
	{ .compatible = "img,powervr-sgx531", .data = &sgx531_data, },
	{ .compatible = "img,powervr-sgx535", .data = &sgx520_data, },
	{ .compatible = "img,powervr-sgx540", .data = &sgx531_data, },
	{ .compatible = "img,powervr-sgx543", .data = &sgx531_data, },
	{ .compatible = "img,powervr-sgx544", .data = &sgx531_data, },
	{ .compatible = "img,powervr-sgx545", .data = &sgx545_data, },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver rewopsgx_driver = {
	.probe		= rewopsgx_probe,
	.remove		= rewopsgx_remove,
	.driver		= {
		.name	= "rewopsgx",
		.of_match_table = dt_match,
	},
};
module_platform_driver(rewopsgx_driver);

MODULE_AUTHOR("RewopSGX Project Developers");
MODULE_DESCRIPTION("RewopSGX DRM Driver");
MODULE_LICENSE("GPL v2");
