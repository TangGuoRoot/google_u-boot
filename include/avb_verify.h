
/*
 * (C) Copyright 2018, Linaro Limited
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef	_AVB_VERIFY_H
#define _AVB_VERIFY_H

#include <../lib/libavb/libavb.h>
#include <blk.h>
#include <command.h>
#include <mapmem.h>
#include <part.h>

#define AVB_MAX_ARGS			1024
#define VERITY_TABLE_OPT_RESTART	"restart_on_corruption"
#define VERITY_TABLE_OPT_LOGGING	"ignore_corruption"
#define ALLOWED_BUF_ALIGN		8

enum avb_boot_state {
	AVB_GREEN,
	AVB_YELLOW,
	AVB_ORANGE,
	AVB_RED,
};

struct preloaded_partition {
	uint8_t *addr;
	size_t size; // 0 means the partition hasn't yet been preloaded
};

struct AvbOpsData {
	struct AvbOps ops;
	const char *iface;
	const char *devnum;
	enum avb_boot_state boot_state;
#ifdef CONFIG_OPTEE_TA_AVB
	struct udevice *tee;
	u32 session;
#endif
	const char *slot_suffix;
	struct preloaded_partition boot;
	struct preloaded_partition vendor_boot;
	struct preloaded_partition init_boot;
};

struct avb_part {
	struct blk_desc *blk;
	struct disk_partition info;
};

enum io_type {
	IO_READ,
	IO_WRITE
};

AvbOps *avb_ops_alloc(const char *iface, const char *devnum);
void avb_ops_free(AvbOps *ops);

char *avb_set_state(AvbOps *ops, enum avb_boot_state boot_state);
char *avb_set_enforce_verity(const char *cmdline);
char *avb_set_ignore_corruption(const char *cmdline);

/**
 * Verify the requested partitions
 *
 * Returns AvbSlotVerifyData and kernel command line parameters as out arguments and either
 * CMD_RET_SUCCESS or CMD_RET_FAILURE as the return value.
 */
int avb_verify_partitions(struct AvbOps *ops,
	       const char *slot_suffix,
	       const char * const requested_partitions[],
	       AvbSlotVerifyData **out_data,
	       char **out_cmdline);

/**
 * Verify vbmeta, any chained vbmeta, boot, and vendor_boot partitions
 *
 * Returns AvbSlotVerifyData and kernel command line parameters as out arguments and either
 * CMD_RET_SUCCESS or CMD_RET_FAILURE as the return value.
 */
int avb_verify(struct AvbOps *ops,
	       const char *slot_suffix,
	       AvbSlotVerifyData **out_data,
	       char **out_cmdline);

/**
 * Extract public key used to sign partitions in AvbSlotVerifyData.
 *
 * Returns CMD_RET_SUCCESS or CMD_RET_FAILURE as the return value.
 */
int avb_find_main_pubkey(const AvbSlotVerifyData *data,
			 const uint8_t **key, size_t *size);

/**
 * Verify that a key is trusted by the platform
 *
 * Returns CMD_RET_SUCCESS or CMD_RET_FAILURE as the return value.
 */
int avb_pubkey_is_trusted(const uint8_t *key, size_t size);

/**
 * ============================================================================
 * I/O helper inline functions
 * ============================================================================
 */
static inline uint64_t calc_offset(struct avb_part *part, int64_t offset)
{
	u64 part_size = part->info.size * part->info.blksz;

	if (offset < 0)
		return part_size + offset;

	return offset;
}

#endif /* _AVB_VERIFY_H */
