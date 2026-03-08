#include <stdint.h>
#include <string.h>
#include "virtio.h"
#include "virtio_wrapper.h"
#include "virtio_mmio.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "virtio_console.h"

/*
 * Copyright (c) 2026 Beijing Institute of Open Source Chip (BOSC)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

struct virtio_mmio_dev_info {
	const char name[64];
	struct virtio_emulator* (*create)(void);
};

static struct virtio_mmio_dev_info virtio_dev_info[] = {
	{ VIRTIO_EMU_NAME_BLK,     virtio_blk_emulator_create },
	{ VIRTIO_EMU_NAME_NET,     virtio_net_emulator_create },
	{ VIRTIO_EMU_NAME_CONSOLE, virtio_console_emulator_create },
};
#define VIRTIO_DEV_INFO_CNT (sizeof(virtio_dev_info) / sizeof(virtio_dev_info[0]))

void virtio_process_req(virtio_handle_t handle)
{
	struct virtio_mmio_dev *dev = handle;
	struct libvirtio_callback *cb = &dev->cb;

	if (!dev || !cb || !cb->process_req)
		return;

	cb->process_req(cb->data);
}

int virtio_receive(virtio_handle_t handle, void *buf, int len)
{
	struct virtio_mmio_dev *dev = handle;
	struct libvirtio_callback *cb = &dev->cb;

	if (!dev || !cb || !cb->receive)
		return -1;

	return cb->receive(buf, len, cb->data);
}

int virtio_mmio_read(virtio_handle_t handle, uint64_t addr,
		     uint32_t *val, int len)
{
	struct virtio_mmio_dev *dev = handle;

	if (!dev)
		return -1;

	return virtio_dev_mmio_read(dev, (uint32_t)(addr - dev->start), val, len);
}

int virtio_mmio_write(virtio_handle_t handle, uint64_t addr, uint32_t val,
		      int len, int *is_doorbell)
{
	struct virtio_mmio_dev *dev = handle;

	if (!dev)
		return -1;

	return virtio_dev_mmio_write(dev, (uint32_t)(addr - dev->start), val, len, is_doorbell);
}

void virtio_mmio_show_all(void)
{
	virtio_mmio_dev_show_all();
}

virtio_handle_t virtio_mmio_create(const char *name, uint64_t start, int len,
				   struct libvirtio_ops *ops, void *priv)
{
	struct virtio_mmio_dev *dev;
	struct virtio_emulator *emu = NULL;
	int i;

	dev = virtio_dev_mmio_create(start, start + len, ops);
	if (!dev)
		return NULL;

	dev->priv = priv;

	for (i = 0; i < VIRTIO_DEV_INFO_CNT; i++) {
		if (!strcmp(virtio_dev_info[i].name, name))
			emu = virtio_dev_info[i].create();
	}
	if (!emu)
		return NULL;

	virtio_mmio_dev_connect(name, dev, emu);

	return dev;
}
