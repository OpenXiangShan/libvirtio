#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#include "list.h"
#include "virtio.h"

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define s8 int8_t
#define s16 int16_t
#define s32 int32_t
#define s64 int64_t

#define USHRT_MAX	((u16)(~0U))
#define SHRT_MAX	((s16)(USHRT_MAX>>1))
#define SHRT_MIN	((s16)(-SHRT_MAX - 1))
#define INT_MAX		((int)(~0U>>1))
#define INT_MIN		(-INT_MAX - 1)
#define UINT_MAX	(~0U)
#define LONG_MAX	((long)(~0UL>>1))
#define LONG_MIN	(-LONG_MAX - 1)
#define ULONG_MAX	(~0UL)
#define LLONG_MAX	((long long)(~0ULL>>1))
#define LLONG_MIN	(-LLONG_MAX - 1)
#define ULLONG_MAX	(~0ULL)

extern u32 do_udiv32(u32 dividend, u32 divisor, u32 * remainder);
static inline u32 umod32(u32 value, u32 divisor)
{
	u32 r;
	do_udiv32(value, divisor, &r);
	return r;
}

int memory_region_is_overlay(unsigned long start, unsigned long end,
				    unsigned long new_start,
				    unsigned long new_end);

int libvirtio_gphys_read(struct virtio_device *dev, uint64_t gpa, void *dst, uint32_t len);
int libvirtio_gphys_write(struct virtio_device *dev, uint64_t gpa, void *src, uint32_t len);
int libvirtio_gphys_map(struct virtio_device *dev,
			uint64_t gphys_addr, uint64_t gphys_size,
			uint64_t *hphys_addr, uint64_t *hphys_size);
void libvirtio_print(struct virtio_device *dev, const char *fmt, ...);
uint64_t libvirtio_alloc(struct virtio_device *dev, int size);
uint64_t libvirtio_zalloc(struct virtio_device *dev, int size);
void libvirtio_free(struct virtio_device *dev, uint64_t addr, int size);

/* virtio blk */
int libvirtio_get_blk_capacity(struct virtio_device *dev);
int libvirtio_submit_blk_io(struct virtio_device *dev,
			    uint64_t sector, void *buf, int len,
			    uint8_t flags);

/* virtio net */
int libvirtio_net_read_tap(struct virtio_device *dev, uint64_t offset,
			   void *buf, int len);
int libvirtio_net_write_tap(struct virtio_device *dev, uint64_t offset,
			    void *buf, int len);
int libvirtio_net_ctrl_mq(struct virtio_device *dev, int vq_pairs);
int libvirtio_net_set_mac(struct virtio_device *dev, uint8_t *mac);

int libvirtio_set_irq(struct virtio_device *dev);

#define my_net_read_tap         libvirtio_net_read_tap
#define my_net_write_tap        libvirtio_net_write_tap
#define my_net_ctrl_mq          libvirtio_net_ctrl_mq
#define my_net_set_mac          libvirtio_net_set_mac
#define my_set_irq              libvirtio_set_irq
#define my_get_blk_capacity     libvirtio_get_blk_capacity
#define my_submit_blk_request   libvirtio_submit_blk_io
#define my_guest_physical_read  libvirtio_gphys_read
#define my_guest_physical_write libvirtio_gphys_write
#define my_guest_physical_map   libvirtio_gphys_map
#define my_alloc                libvirtio_alloc
#define my_zalloc               libvirtio_zalloc
#define my_free                 libvirtio_free
#define my_print                libvirtio_print

#endif
