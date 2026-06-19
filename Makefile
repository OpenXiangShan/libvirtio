CC := gcc
AR := ar
CFLAGS := -Wall -g -O0
SLIRP_CFLAGS := $(shell pkg-config --cflags slirp 2>/dev/null)
VIRTIO_CFLAGS := -Ivirtio
BACKEND_CFLAGS := -Ibackend

VIRTIO_SRCS := virtio/fifo.c virtio/utils.c virtio/virtio.c \
	virtio/virtio_blk.c virtio/virtio_console.c virtio/virtio_mmio.c \
	virtio/virtio_net.c virtio/virtio_gpu.c virtio/virtio_pci.c \
	virtio/virtio_wrapper.c
VIRTIO_OBJS := $(patsubst virtio/%.c,build/virtio/%.o,$(VIRTIO_SRCS))
BACKEND_SRCS := backend/virtio_backend.c backend/virtio_backend_queue.c \
	backend/virtio_backend_blk.c backend/virtio_backend_console.c \
	backend/virtio_backend_net.c backend/virtio_backend_gpu.c
BACKEND_OBJS := $(patsubst backend/%.c,build/backend/%.backend.o,$(BACKEND_SRCS))
TARGET := libMyVirtio.a
BACKEND_TARGET := libMyVirtio_backend.a

.PHONY: all clean

all: $(TARGET) $(BACKEND_TARGET)

$(TARGET): $(VIRTIO_OBJS)
	@mkdir -p output
	$(AR) rcs $@ $^
	@cp $@ output/
	@cp virtio/virtio_wrapper.h output/

$(BACKEND_TARGET): $(BACKEND_OBJS)
	@mkdir -p output
	$(AR) rcs $@ $^
	@cp $@ output/
	@cp backend/virtio_backend.h output/

build/virtio/%.o: virtio/%.c
	@mkdir -p build/virtio
	$(CC) $(CFLAGS) $(VIRTIO_CFLAGS) -c $< -o $@

build/backend/%.backend.o: backend/%.c
	@mkdir -p build/backend
	$(CC) $(CFLAGS) $(BACKEND_CFLAGS) $(SLIRP_CFLAGS) -c $< -o $@

clean:
	rm -rf build
	rm -f *.o *.backend.o virtio/*.o backend/*.backend.o
	rm -f $(TARGET) $(BACKEND_TARGET)
	rm -rf output
