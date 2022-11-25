// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2019-2020, Renesas Electronics Corporation
 */

#include <io.h>
#include <trace.h>
#include <initcall.h>
#include <mm/core_memprot.h>
#include <kernel/misc.h>
#include "rzg_suspend_to_ram.h"

extern struct reg_backup_info *__suspend_to_ram_backup_start;
extern struct reg_backup_info *__suspend_to_ram_backup_end;
extern size_t __suspend_to_ram_backup_num_start;
extern size_t __suspend_to_ram_backup_num_end;
extern backup_call_t __suspend_to_ram_cbfunc_start;
extern backup_call_t __suspend_to_ram_cbfunc_end;

typedef void (*backup_func_t)(struct reg_backup_info *bkinfo);

static void call_backup_funcs(backup_func_t backup_func);
static void convert_p2v_register(struct reg_backup_info *bkinfo);
static void read_register(struct reg_backup_info *bkinfo,
			union reg_backup_buf *bkbuf);
static void write_register(struct reg_backup_info *bkinfo,
			union reg_backup_buf *bkbuf);
static void save_register(struct reg_backup_info *bkinfo);
static void restore_register(struct reg_backup_info *bkinfo);
static TEE_Result suspend_to_ram_init(void);

static void call_backup_funcs(backup_func_t backup_func)
{
	struct reg_backup_info **section_info;
	struct reg_backup_info *bkinfo;
	size_t *section_info_num;
	size_t bkinfo_num;
	size_t cnt;

	section_info = &__suspend_to_ram_backup_start;
	section_info_num = &__suspend_to_ram_backup_num_start;

	for (; section_info < &__suspend_to_ram_backup_end; section_info++) {
		bkinfo = *section_info;
		bkinfo_num = *section_info_num;
		for(cnt = 0U; cnt < bkinfo_num; cnt++) {
			if (backup_func != NULL) {
				backup_func(&bkinfo[cnt]);
			}
		}
		section_info_num++;
	}
}

static void convert_p2v_register(struct reg_backup_info *bkinfo)
{
	bkinfo->reg_vaddr = (vaddr_t)phys_to_virt(bkinfo->reg_paddr,
					MEM_AREA_IO_SEC, 0x4);
	if (bkinfo->reg_vaddr == 0U) {
		bkinfo->reg_vaddr = (vaddr_t)phys_to_virt(bkinfo->reg_paddr,
						MEM_AREA_IO_NSEC, 0x4);
		if (bkinfo->reg_vaddr == 0U) {
			EMSG("Convert error! phys_to_virt reg_paddr=%08zX",
				bkinfo->reg_paddr);
		}
	}
	bkinfo->reg_buf.d32 = 0U;
}

static void read_register(struct reg_backup_info *bkinfo,
			union reg_backup_buf *bkbuf)
{
	if (bkinfo->reg_vaddr != 0U) {
		if (bkinfo->reg_rsize == 4) {
			bkbuf->d32 = io_read32(bkinfo->reg_vaddr);
		} else if (bkinfo->reg_rsize == 2) {
			bkbuf->d16 = io_read16(bkinfo->reg_vaddr);
		} else if (bkinfo->reg_rsize == 1) {
			bkbuf->d8 = io_read8(bkinfo->reg_vaddr);
		} else {
			EMSG("Invalid reg_rsize=%d. reg_paddr=%08zX",
				bkinfo->reg_rsize, bkinfo->reg_paddr);
		}
	} else {
		EMSG("Read skip. reg_paddr=%08zX", bkinfo->reg_paddr);
	}
}

static void write_register(struct reg_backup_info *bkinfo,
			union reg_backup_buf *bkbuf)
{
	if (bkinfo->reg_vaddr != 0U) {
		if (bkinfo->reg_wsize == 4) {
			io_write32(bkbuf->d32, bkinfo->reg_vaddr);
		} else if (bkinfo->reg_wsize == 2) {
			io_write16(bkbuf->d16, bkinfo->reg_vaddr);
		} else if (bkinfo->reg_wsize == 1) {
			io_write8(bkbuf->d8, bkinfo->reg_vaddr);
		} else {
			EMSG("Invalid reg_wsize=%d. reg_paddr=%08zX",
				bkinfo->reg_wsize, bkinfo->reg_paddr);
		}
	} else {
		EMSG("Restore skip. reg_paddr=%08zX", bkinfo->reg_paddr);
	}
}

static void save_register(struct reg_backup_info *bkinfo)
{
	read_register(bkinfo, &bkinfo->reg_buf);
}

static void restore_register(struct reg_backup_info *bkinfo)
{
	write_register(bkinfo, &bkinfo->reg_buf);
}

static TEE_Result suspend_to_ram_init(void)
{
	call_backup_funcs(convert_p2v_register);

	return TEE_SUCCESS;
}

driver_init(suspend_to_ram_init);

void suspend_to_ram_save(void)
{
	call_backup_funcs(save_register);
	suspend_to_ram_call_cbfunc(SUS2RAM_STATE_SUSPEND);
}

void suspend_to_ram_restore(void)
{
	call_backup_funcs(restore_register);
	suspend_to_ram_call_cbfunc(SUS2RAM_STATE_RESUME);
}

void suspend_to_ram_call_cbfunc(enum suspend_to_ram_state state)
{
	backup_call_t *cbfunc;
	uint32_t cpu_id;

	cbfunc = &__suspend_to_ram_cbfunc_start;
	cpu_id = get_core_pos();

	for (; cbfunc < &__suspend_to_ram_cbfunc_end; cbfunc++) {
		(*cbfunc)(state, cpu_id);
	}
}

void suspend_to_ram_init_helper(struct reg_backup_info *bkarray,
			size_t array_num)
{
	size_t cnt;

	for (cnt = 0U; cnt < array_num; cnt++) {
		convert_p2v_register(&bkarray[cnt]);
	}
}

void suspend_to_ram_save_helper(struct reg_backup_info *bkarray,
			size_t array_num)
{
	size_t cnt;

	for (cnt = 0U; cnt < array_num; cnt++) {
		save_register(&bkarray[cnt]);
	}
}

void suspend_to_ram_restore_helper(struct reg_backup_info *bkarray,
			size_t array_num)
{
	size_t cnt;

	for (cnt = 0U; cnt < array_num; cnt++) {
		restore_register(&bkarray[cnt]);
	}
}
