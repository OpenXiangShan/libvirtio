#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "virtio.h"
#include "virtio_blk.h"
#include "virtio_mmio.h"
#include "virtio_wrapper.h"

#define BASE 0x10000000UL
static unsigned char memory[0x10000];
static unsigned int backend_writes;
static int fail_dma;

static uint64_t alloc(int size)
{
	return (uint64_t)calloc(1, size);
}

static void release(uint64_t ptr, int size)
{
	free((void *)ptr);
}

static int read_mem(uint64_t addr, void *buf, uint32_t len, void *priv)
{
	if (addr + len > sizeof(memory) || (addr == 0x5000 && fail_dma))
		return -1;
	memcpy(buf, memory + addr, len);
	return len;
}

static int write_mem(uint64_t addr, void *buf, uint32_t len, void *priv)
{
	if (addr + len > sizeof(memory) || (addr == 0x5000 && fail_dma))
		return -1;
	memcpy(memory + addr, buf, len);
	return len;
}

static int submit(uint64_t sector, void *buf, int len, uint8_t flags,
		  void *priv)
{
	if (flags == MY_BLK_REQ_READ)
		memset(buf, 0x5a, len);
	else if (flags == MY_BLK_REQ_WRITE)
		backend_writes++;
	return 0;
}

static int irq(void *priv)
{
	return 0;
}

static void reg_write(virtio_handle_t dev, unsigned off, unsigned value)
{
	int doorbell;
	assert(!virtio_mmio_write(dev, BASE + off, value, 4, &doorbell));
}

static void test_transport(bool modern)
{
	memset(memory, 0, sizeof(memory));
	backend_writes = 0;
	struct libvirtio_ops ops = { .mm_alloc = alloc,
				     .mm_free = release,
				     .guest_mem_read = read_mem,
				     .guest_mem_write = write_mem,
				     .set_irq = irq,
				     .blk_ops.submit_blk_io = submit };
	struct virtio_mmio_options options = { .modern = modern };
	virtio_handle_t dev = virtio_mmio_create_ex(VIRTIO_EMU_NAME_BLK, BASE,
						    0x1000, &ops, 0, &options);
	assert(dev);
	reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_SEL, 0);
	reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_NUM, 8);
	if (modern) {
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_DESC_LOW, 0x1000);
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_AVAIL_LOW, 0x2000);
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_USED_LOW, 0x3000);
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_READY, 1);
	} else {
		reg_write(dev, VMM_VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_ALIGN, 4096);
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_PFN, 1);
	}
	struct vring_desc *desc = (void *)(memory + 0x1000);
	struct vring_avail *avail =
	    (void *)(memory + (modern ? 0x2000 : 0x1080));
	struct vring_used *used = (void *)(memory + (modern ? 0x3000 : 0x2000));
	struct virtio_blk_outhdr *hdr = (void *)(memory + 0x4000);
	for (int run = 0; run < 4; run++) {
		bool write = run & 1;
		fail_dma = run < 2;
		hdr->type = write ? VMM_VIRTIO_BLK_T_OUT : VMM_VIRTIO_BLK_T_IN;
		desc[0] = (struct vring_desc){ .addr = 0x4000,
					       .len = sizeof(*hdr),
					       .flags = VMM_VRING_DESC_F_NEXT,
					       .next = 1 };
		desc[1] = (struct vring_desc){
			.addr = 0x5000,
			.len = 512,
			.flags = VMM_VRING_DESC_F_NEXT |
				 (write ? 0 : VMM_VRING_DESC_F_WRITE),
			.next = 2
		};
		desc[2] =
		    (struct vring_desc){ .addr = 0x6000,
					 .len = 1,
					 .flags = VMM_VRING_DESC_F_WRITE };
		memory[0x6000] = 0xff;
		avail->ring[run] = 0;
		avail->idx = run + 1;
		reg_write(dev, VMM_VIRTIO_MMIO_QUEUE_NOTIFY, 0);
		virtio_process_req(dev);
		assert(used->idx == run + 1);
		assert(memory[0x6000] == (fail_dma ? VMM_VIRTIO_BLK_S_IOERR
						   : VMM_VIRTIO_BLK_S_OK));
		assert(backend_writes == (run == 3 ? 1 : 0));
	}
	virtio_mmio_destroy(dev);
}

int main(void)
{
	test_transport(false);
	test_transport(true);
	puts("VIRTIO_BLK_DMA_ERROR_PASS legacy=8 modern=8");
}
