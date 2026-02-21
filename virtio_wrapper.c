#include <stdint.h>
#include <string.h>
#include "virtio.h"
#include "virtio_wrapper.h"
#include "virtio_mmio.h"
#include "virtio_blk.h"
#include "virtio_net.h"

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
	{ VIRTIO_EMU_NAME_BLK, virtio_blk_emulator_create },
	{ VIRTIO_EMU_NAME_NET, virtio_net_emulator_create },
};
#define VIRTIO_DEV_INFO_CNT (sizeof(virtio_dev_info) / sizeof(virtio_dev_info[0]))

struct libvirtio_callback *libvirtio_get_callback(uint64_t addr)
{
	struct virtio_mmio_dev *mdev;

	mdev = virtio_mmio_dev_get(addr, 1);
	if (!mdev)
		return NULL;

	return &mdev->cb;
}

int virtio_mmio_read(uint64_t addr, uint32_t *val, int len)
{
	struct virtio_mmio_dev *dev;

	dev = virtio_mmio_dev_get(addr, len);
	if (!dev)
		return -1;

	return virtio_dev_mmio_read(dev, (uint32_t)(addr - dev->start), val, len);
}

int virtio_mmio_write(uint64_t addr, uint32_t val, int len)
{
	struct virtio_mmio_dev *dev;

	dev = virtio_mmio_dev_get(addr, len);
	if (!dev)
		return -1;

	return virtio_dev_mmio_write(dev, (uint32_t)(addr - dev->start), val, len);
}

int virtio_mmio_create(const char *name, uint64_t start, int len,
		       struct libvirtio_ops *ops, void *priv)
{
	struct virtio_mmio_dev *dev;
	struct virtio_emulator *emu = NULL;
	int i;

	dev = virtio_dev_mmio_create(start, start + len, ops);
	if (!dev)
		return -1;

	dev->priv = priv;

	for (i = 0; i < VIRTIO_DEV_INFO_CNT; i++) {
		if (!strcmp(virtio_dev_info[i].name, name))
			emu = virtio_dev_info[i].create();
	}
	if (!emu)
		return -1;

	virtio_mmio_dev_connect(name, dev, emu);

	return 0;
}
