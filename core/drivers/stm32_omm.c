// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021-2022, STMicroelectronics
 */

#include <arm.h>
#include <config.h>
#include <drivers/clk.h>
#include <drivers/clk_dt.h>
#include <drivers/stm32_firewall.h>
#include <drivers/stm32_gpio.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stm32_sysconf.h>
#include <stm32_util.h>
#include <trace.h>

/*
 * OCTOSPI registers
 */
#define _OCTOSPI_CR			U(0x0000)

/*
 * OCTOSPI CR register bitfields
 */
#define _OCTOSPI_CR_EN			BIT(0)

/*
 * OCTOSPIM registers
 */
#define _OCTOSPIM_CR			U(0x0000)

/*
 * OCTOSPIM CR register bitfields
 */

#define _OCTOSPIM_CR_MUXEN		BIT(0)
#define _OCTOSPIM_CR_MUXEN_MODE_MASK	GENMASK_32(1, 0)
#define _OCTOSPIM_CR_CSSEL_OVR_EN	BIT(4)
#define _OCTOSPIM_CR_CSSEL_OVR_MASK	GENMASK_32(6, 4)
#define _OCTOSPIM_CR_CSSEL_OVR_SHIFT	U(5)
#define _OCTOSPIM_CR_REQ2ACK_MASK	GENMASK_32(23, 16)
#define _OCTOSPIM_CR_REQ2ACK_SHIFT	U(16)

#define OSPI_NB				U(2)
#define OSPIM_NSEC_PER_SEC		UL(1000000000)

struct stm32_mm_region {
	paddr_t start;
	paddr_t end;
};

struct stm32_ospi_pdata {
	struct clk *clock;
	struct stm32_mm_region region;
	vaddr_t base;
};

struct stm32_omm_pdata {
	struct clk *clock;
	struct stm32_pinctrl_list *pinctrl_l;
	struct stm32_ospi_pdata ospi_d[OSPI_NB];
	struct stm32_mm_region region;
	void *firewall_dt_config;
	int firewall_nb_config;
	vaddr_t base;
	uint32_t mux;
	uint32_t cssel_ovr;
	uint32_t req2ack;
};

static struct stm32_omm_pdata *omm_d;

static TEE_Result stm32_omm_parse_fdt(const void *fdt, int node)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	const fdt32_t *cuint = NULL;
	struct io_pa_va addr = {};
	paddr_t base = DT_INFO_INVALID_REG;
	size_t size = DT_INFO_INVALID_REG_SIZE;
	int ctrl_node = 0;
	int len = 0;
	int ac = 0;
	int parent = 0;
	unsigned int i = 0;
	uint32_t ospi_i = 0;
	paddr_t ospi_address[OSPI_NB] = {};
	uint8_t ospi_assigned = 0;

	if (_fdt_get_reg_props_by_name(fdt, node, "omm", &base, &size) < 0)
		panic();

	addr.pa = base;
	omm_d->base = io_pa_or_va(&addr, size);

	if (_fdt_get_reg_props_by_name(fdt, node, "omm_mm", &base, &size) < 0)
		panic();

	omm_d->region.start = base;
	omm_d->region.end = base + size;

	res = clk_dt_get_by_index(fdt, node, 0, &omm_d->clock);
	if (res)
		return res;

	res = stm32_pinctrl_dt_get_by_index(fdt, node, 0, &omm_d->pinctrl_l);
	if (res && res != TEE_ERROR_ITEM_NOT_FOUND)
		return res;

	res = stm32_firewall_get_data_config(virt_to_phys((void *)omm_d->base),
					     0, fdt, node,
					     &omm_d->firewall_nb_config,
					     &omm_d->firewall_dt_config);
	if (res) {
		if (res == TEE_ERROR_ITEM_NOT_FOUND)
			res = TEE_ERROR_DEFER_DRIVER_INIT;

		return res;
	}

	omm_d->mux = _fdt_read_uint32_default(fdt, node, "st,omm-mux", 0);
	omm_d->req2ack = _fdt_read_uint32_default(fdt, node,
						  "st,omm-req2ack-ns", 0);
	omm_d->cssel_ovr = _fdt_read_uint32_default(fdt, node,
						    "st,omm-cssel-ovr", 0xff);

	parent = fdt_parent_offset(fdt, node);
	if (parent < 0)
		panic();

	ac = fdt_address_cells(fdt, parent);
	if (ac < 0)
		panic();

	cuint = fdt_getprop(fdt, node, "ranges", NULL);
	if (!cuint)
		panic();

	for (i = 0; i < OSPI_NB; i++) {
		ospi_i = fdt32_to_cpu(*cuint);
		if (ospi_i >= OSPI_NB || (ospi_assigned & BIT(ospi_i)))
			panic();

		ospi_assigned |= BIT(ospi_i);
		ospi_address[ospi_i] = _fdt_read_paddr(cuint + 2, ac);
		if (ospi_address[ospi_i] == DT_INFO_INVALID_REG)
			panic();

		cuint += 4;
	}

	cuint = fdt_getprop(fdt, node, "memory-region", &len);
	if (cuint) {
		const char *mm_name[OSPI_NB] = { "mm_ospi1", "mm_ospi2" };
		struct stm32_mm_region *region = NULL;
		uint32_t offset = 0;
		int pnode = 0;
		int index = 0;

		for (i = 0; i < OSPI_NB; i++) {
			index = fdt_stringlist_search(fdt, node,
						      "memory-region-names",
						      mm_name[i]);
			if (index < 0)
				continue;

			if (index >= (int)(len / sizeof(uint32_t)))
				panic();

			offset = fdt32_to_cpu(*(cuint + index));
			pnode = fdt_node_offset_by_phandle(fdt, offset);
			if (pnode < 0)
				panic();

			region = &omm_d->ospi_d[i].region;
			region->start = _fdt_reg_base_address(fdt, pnode);
			if (region->start == DT_INFO_INVALID_REG)
				panic();

			size = _fdt_reg_size(fdt, pnode);
			if (size == DT_INFO_INVALID_REG_SIZE)
				panic();

			region->end = region->start + size;
		}
	}

	ospi_i = 0;
	fdt_for_each_subnode(ctrl_node, fdt, node)
		ospi_i++;

	if (ospi_i != OSPI_NB)
		panic();

	ospi_assigned = 0;
	fdt_for_each_subnode(ctrl_node, fdt, node) {
		cuint = fdt_getprop(fdt, ctrl_node, "reg", NULL);
		if (!cuint)
			panic();

		ospi_i = fdt32_to_cpu(*cuint);
		if (ospi_i >= OSPI_NB || (ospi_assigned & BIT(ospi_i)))
			panic();

		ospi_assigned |= BIT(ospi_i);
		addr.pa = fdt32_to_cpu(*(cuint + 1)) + ospi_address[ospi_i];
		addr.va = 0;
		size = fdt32_to_cpu(*(cuint + 2));
		omm_d->ospi_d[ospi_i].base = io_pa_or_va(&addr, size);

		res = clk_dt_get_by_index(fdt, ctrl_node, 0,
					  &omm_d->ospi_d[ospi_i].clock);
		if (res)
			return res;
	}

	return TEE_SUCCESS;
}

static bool stm32_omm_region_contains(struct stm32_mm_region *r1,
				      struct stm32_mm_region *r2)
{
	return r1->start <= r2->start && r1->end >= r2->end;
}

static bool stm32_omm_region_overlaps(struct stm32_mm_region *r1,
				      struct stm32_mm_region *r2)
{
	return r1->start <= r2->end && r1->end >= r2->start;
}

static void stm32_omm_set_mm(void)
{
	unsigned int ospi_i = 0;
	struct stm32_mm_region *region[OSPI_NB] = {};
	size_t mm_size[OSPI_NB] = {};
	unsigned int valid_regions = 0;

	for (ospi_i = 0; ospi_i < OSPI_NB; ospi_i++) {
		region[ospi_i] = &omm_d->ospi_d[ospi_i].region;

		if (region[ospi_i]->start >= region[ospi_i]->end)
			continue;

		if (!stm32_omm_region_contains(&omm_d->region, region[ospi_i]))
			panic();

		mm_size[ospi_i] = region[ospi_i]->end - region[ospi_i]->start;
		valid_regions++;
	}

	if (valid_regions == OSPI_NB &&
	    !stm32_omm_region_overlaps(region[0], region[1]))
		panic();

	if (valid_regions)
		stm32mp25_syscfg_set_amcr(mm_size[0], mm_size[1]);
}

static void stm32_omm_configure(void)
{
	unsigned int ospi_i = 0;
	unsigned long clk_rate_max = 0;
	uint32_t cssel_ovr = 0;
	uint32_t req2ack = 0;

	for (ospi_i = 0; ospi_i < OSPI_NB; ospi_i++) {
		clk_rate_max = MAX(clk_get_rate(omm_d->ospi_d[ospi_i].clock),
				   clk_rate_max);

		if (clk_enable(omm_d->ospi_d[ospi_i].clock))
			panic();

		io_clrbits32(omm_d->ospi_d[ospi_i].base + _OCTOSPI_CR,
			     _OCTOSPI_CR_EN);

		clk_disable(omm_d->ospi_d[ospi_i].clock);
	}

	if (omm_d->mux & _OCTOSPIM_CR_MUXEN && omm_d->req2ack) {
		unsigned long hclkn = OSPIM_NSEC_PER_SEC / clk_rate_max;
		unsigned long timing = DIV_ROUND_UP(omm_d->req2ack, hclkn) - 1;

		if (timing >
		    _OCTOSPIM_CR_REQ2ACK_MASK >> _OCTOSPIM_CR_REQ2ACK_SHIFT)
			req2ack = _OCTOSPIM_CR_REQ2ACK_MASK;
		else
			req2ack = timing << _OCTOSPIM_CR_REQ2ACK_SHIFT;
	}

	if (omm_d->cssel_ovr != 0xff)
		cssel_ovr = (omm_d->cssel_ovr << _OCTOSPIM_CR_CSSEL_OVR_SHIFT) |
			    _OCTOSPIM_CR_CSSEL_OVR_EN;

	if (clk_enable(omm_d->clock))
		panic();

	io_clrsetbits32(omm_d->base + _OCTOSPIM_CR,
			_OCTOSPIM_CR_REQ2ACK_MASK |
			_OCTOSPIM_CR_CSSEL_OVR_MASK |
			_OCTOSPIM_CR_MUXEN_MODE_MASK,
			req2ack | cssel_ovr | omm_d->mux);

	clk_disable(omm_d->clock);
}

static TEE_Result stm32_omm_probe(const void *fdt, int node,
				  const void *compat_data __unused)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	omm_d = calloc(1, sizeof(*omm_d));
	if (!omm_d)
		return TEE_ERROR_OUT_OF_MEMORY;

	res = stm32_omm_parse_fdt(fdt, node);
	if (res)
		goto err_free;

	stm32_omm_set_mm();
	stm32_omm_configure();
	if (omm_d->pinctrl_l)
		stm32_pinctrl_load_config(omm_d->pinctrl_l);

	res = stm32_firewall_set_data_config(virt_to_phys((void *)omm_d->base),
					     0, omm_d->firewall_nb_config,
					     omm_d->firewall_dt_config);
	if (res)
		panic();

	return TEE_SUCCESS;

err_free:
	if (stm32_firewall_put_data_config(virt_to_phys((void *)omm_d->base),
					   0, omm_d->firewall_nb_config,
					   omm_d->firewall_dt_config))
		panic();

	free(omm_d);

	return res;
}

static const struct dt_device_match stm32_omm_match_table[] = {
	{ .compatible = "st,stm32mp25-omm" },
	{ }
};

DEFINE_DT_DRIVER(stm32_omm_dt_driver) = {
	.name = "st,stm32mp25-omm",
	.match_table = stm32_omm_match_table,
	.probe = stm32_omm_probe,
};
