CC := gcc
AR := ar
CFLAGS := -Wall -g -O0
SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
TARGET := libMyVirtio.a

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p output
	$(AR) rcs $@ $^
	@cp $@ output/
	@cp virtio_wrapper.h output/

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf output

