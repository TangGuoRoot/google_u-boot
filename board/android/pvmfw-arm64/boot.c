// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Google LLC
 */

#include <asm/global_data.h>
#include <asm/io.h>

#include <android_bootloader.h>
#include <android_image.h>
#include <avb_verify.h>
#include <bcc.h>
#include <command.h>
#include <config.h>
#include <dm/uclass.h>
#include <fdt_support.h>
#include <malloc.h>
#include <rng.h>
#include <string.h>
#include <linux/err.h>

#include "avb_preloaded.h"
#include "generate_fdt.h"

/* Taken from libavb/avb_slot_verify.c */
#define VBMETA_MAX_SIZE			SZ_64K

/* Taken from crosvm */
#define FDT_MAX_SIZE			SZ_2M

DECLARE_GLOBAL_DATA_PTR;

static bool is_valid_ram(const void *ptr)
{
	uintptr_t addr = (uintptr_t)ptr;

	return CONFIG_SYS_SDRAM_BASE <= addr && addr < gd->ram_top;
}

static bool is_valid_ram_region(const void *ptr, size_t size)
{
	return is_valid_ram(ptr) && (size <= gd->ram_top - (uintptr_t)ptr);
}

static struct AvbOps *alloc_avb_ops(void *image, size_t size)
{
	int error;
	AvbFooter footer;
	const void *avb_footer = image + size - AVB_FOOTER_SIZE;
	struct AvbOps *ops = avb_preloaded_alloc();

	if (!ops)
		return NULL;

	if (size < AVB_FOOTER_SIZE ||
	    !avb_footer_validate_and_byteswap(avb_footer, &footer) ||
	    !is_valid_ram_region(image, footer.original_image_size) ||
	    !is_valid_ram_region(image + footer.vbmeta_offset, VBMETA_MAX_SIZE))
		goto free_ops;

	error = avb_preloaded_add_part(ops, "boot", image,
				       footer.original_image_size);
	if (error)
		goto free_ops;

	/*
	 * We can't simply use footer.vbmeta_size as avb_slot_verify() requires
	 * the partition to be VBMETA_MAX_SIZE; as the image was created through
	 * avbtool, there should always be enough 0-padding after the actual
	 * vbmeta data to safely meet that requirement.
	 */
	error = avb_preloaded_add_part(ops, "vbmeta",
				       image + footer.vbmeta_offset,
				       VBMETA_MAX_SIZE);
	if (error)
		goto free_ops;

	return ops;
free_ops:
	avb_preloaded_free(ops);
	return NULL;
}

static int verify_image(void *image, size_t size, struct boot_config *cfg)
{
	const char *instance_uuid = "90d2174a-038a-4bc6-adf3-824848fc5825";
	const char *iface_str = "virtio";
	const int devnum = 1;
	int ret = 0;
	const char *parts[] = { "boot", NULL };
	AvbSlotVerifyData *data = NULL;
	struct AvbOps *ops = NULL;

	ops = alloc_avb_ops(image, size);
	if (!ops)
		return -ENOMEM;

	ret = avb_slot_verify(ops, parts, /*slot_suffix=*/"",
			      AVB_SLOT_VERIFY_FLAGS_NONE,
			      AVB_HASHTREE_ERROR_MODE_RESTART_AND_INVALIDATE,
			      &data);
	if (ret != AVB_SLOT_VERIFY_RESULT_OK) {
		ret = -EACCES;
		goto err;
	}

	cfg->new_instance = false;

	ret = bcc_vm_instance_handover(iface_str, devnum, instance_uuid,
				       /*must_exist=*/false, "vm_entry",
				       BCC_MODE_NORMAL, data, NULL,
				       cfg, sizeof(*cfg));
	if (ret < 0)
		goto err;

	cfg->new_instance = (ret == BCC_VM_INSTANCE_CREATED);
	ret = 0;

err:
	if (data)
		avb_slot_verify_data_free(data);
	if (ops)
		avb_preloaded_free(ops);

	return ret;
}

static int seed_u64(uint64_t *seed)
{
	int res;
	struct udevice *dev;

	res = uclass_get_device_by_name(UCLASS_RNG, "smccc-trng", &dev);
	if (res)
		return res;

	return dm_rng_read(dev, seed, sizeof(*seed));
}

int pvmfw_boot_flow(void *fdt, void *image, size_t size, void *bcc,
		    size_t bcc_size)
{
	int ret;
	struct boot_config cfg = {
		.bcc_addr = virt_to_phys(bcc),
		.bcc_size = bcc_size,
	};

	if (!size || !is_valid_ram_region(image, size)) {
		ret = -EPERM;
		goto err;
	}

	bcc_set_handover(bcc, bcc_size);

	ret = parse_input_fdt(fdt, &cfg);
	if (ret)
		goto err;

	ret = verify_image(image, size, &cfg);
	if (ret)
		goto err;

	ret = transfer_fdt_template(fdt);
	if (ret)
		goto err;

	ret = seed_u64(&cfg.kaslr_seed);
	if (ret)
		goto err;

	ret = patch_output_fdt(fdt, &cfg);

err:
	if (ret)
		bcc_clear_memory(bcc, bcc_size);

	return ret;
}
