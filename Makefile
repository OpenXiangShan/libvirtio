CC := gcc
AR := ar
CMAKE := cmake
CFLAGS := -Wall -g -O0
SLIRP_CFLAGS := $(shell pkg-config --cflags slirp 2>/dev/null)
LIBVNCSERVER_DIR := third_party/libvncserver
LIBVNCSERVER_BUILD_DIR := build/libvncserver
LIBVNCSERVER_ARCHIVE := $(LIBVNCSERVER_BUILD_DIR)/libvncserver.a
BACKEND_MRI := build/libMyVirtio_backend.mri
LIBVNCSERVER_CFLAGS := -I$(LIBVNCSERVER_DIR)/include -I$(LIBVNCSERVER_BUILD_DIR)/include
LIBVNCSERVER_CMAKE_FLAGS := \
	-S $(LIBVNCSERVER_DIR) \
	-B $(LIBVNCSERVER_BUILD_DIR) \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_SHARED_LIBS=OFF \
	-DLIBVNCSERVER_INSTALL=OFF \
	-DWITH_LIBVNCSERVER=ON \
	-DWITH_LIBVNCCLIENT=OFF \
	-DWITH_THREADS=ON \
	-DWITH_24BPP=ON \
	-DWITH_IPv6=ON \
	-DWITH_ZLIB=OFF \
	-DWITH_LZO=OFF \
	-DWITH_JPEG=OFF \
	-DWITH_PNG=OFF \
	-DWITH_SDL=OFF \
	-DWITH_GTK=OFF \
	-DWITH_QT=OFF \
	-DWITH_LIBSSHTUNNEL=OFF \
	-DWITH_GNUTLS=OFF \
	-DWITH_OPENSSL=OFF \
	-DWITH_SYSTEMD=OFF \
	-DWITH_GCRYPT=OFF \
	-DWITH_FFMPEG=OFF \
	-DWITH_TIGHTVNC_FILETRANSFER=OFF \
	-DWITH_WEBSOCKETS=OFF \
	-DWITH_SASL=OFF \
	-DWITH_XCB=OFF \
	-DWITH_EXAMPLES=OFF \
	-DWITH_TESTS=OFF
VIRTIO_CFLAGS := -Ivirtio
BACKEND_CFLAGS := -Ibackend

VIRTIO_SRCS := virtio/fifo.c virtio/utils.c virtio/virtio.c \
	virtio/virtio_blk.c virtio/virtio_console.c virtio/virtio_mmio.c \
	virtio/virtio_net.c virtio/virtio_gpu.c virtio/virtio_input.c \
	virtio/virtio_pci.c \
	virtio/virtio_wrapper.c
VIRTIO_OBJS := $(patsubst virtio/%.c,build/virtio/%.o,$(VIRTIO_SRCS))
BACKEND_SRCS := backend/virtio_backend.c backend/virtio_backend_queue.c \
	backend/virtio_backend_blk.c backend/virtio_backend_console.c \
	backend/virtio_backend_net.c backend/virtio_backend_gpu.c \
	backend/virtio_backend_input.c backend/virtio_backend_ui.c \
	backend/virtio_backend_ui_vnc.c
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

$(LIBVNCSERVER_ARCHIVE):
	$(CMAKE) $(LIBVNCSERVER_CMAKE_FLAGS)
	$(CMAKE) --build $(LIBVNCSERVER_BUILD_DIR) --target vncserver

$(BACKEND_TARGET): $(BACKEND_OBJS) $(LIBVNCSERVER_ARCHIVE)
	@mkdir -p output build
	rm -f $@ $(BACKEND_MRI)
	@{ \
		echo "CREATE $@"; \
		for obj in $(BACKEND_OBJS); do echo "ADDMOD $$obj"; done; \
		echo "ADDLIB $(abspath $(LIBVNCSERVER_ARCHIVE))"; \
		echo "SAVE"; \
		echo "END"; \
	} > $(BACKEND_MRI)
	$(AR) -M < $(BACKEND_MRI)
	$(AR) s $@
	@cp $@ output/
	@cp backend/virtio_backend.h output/

build/virtio/%.o: virtio/%.c
	@mkdir -p build/virtio
	$(CC) $(CFLAGS) $(VIRTIO_CFLAGS) -c $< -o $@

build/backend/%.backend.o: backend/%.c
	@mkdir -p build/backend
	$(CC) $(CFLAGS) $(BACKEND_CFLAGS) $(SLIRP_CFLAGS) $(LIBVNCSERVER_CFLAGS) -c $< -o $@

build/backend/virtio_backend_ui_vnc.backend.o: $(LIBVNCSERVER_ARCHIVE)

clean:
	rm -rf build
	rm -f *.o *.backend.o virtio/*.o backend/*.backend.o
	rm -f $(TARGET) $(BACKEND_TARGET)
	rm -rf output
