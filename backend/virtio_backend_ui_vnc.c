#include "virtio_backend_internal.h"

#include <errno.h>
#include <linux/input.h>
#include <pthread.h>
#include <rfb/keysym.h>
#include <rfb/rfb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VIRTIO_BACKEND_UI_INPUT_QUEUE_DEPTH 1024
#define VIRTIO_BACKEND_UI_DEFAULT_LISTEN "127.0.0.1:5915"

struct virtio_backend_ui_vnc {
	struct virtio_backend_ui base;
	rfbScreenInfoPtr screen;
	struct virtio_backend_queue keyboardq;
	struct virtio_backend_queue mouseq;
	pthread_mutex_t lock;
	uint8_t *fb;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	int lock_initialized;
	int last_x;
	int last_y;
	int pointer_initialized;
	int buttons;
};

static int parse_vnc_listen(const char *listen, char **host, int *port)
{
	const char *spec = listen && *listen ? listen :
						  VIRTIO_BACKEND_UI_DEFAULT_LISTEN;
	const char *colon = strrchr(spec, ':');
	char *end;
	long value;

	if (!colon || colon == spec || !colon[1])
		return -EINVAL;

	value = strtol(colon + 1, &end, 10);
	if (*end || value <= 0 || value > 65535)
		return -EINVAL;

	*host = malloc((size_t)(colon - spec) + 1);
	if (!*host)
		return -ENOMEM;
	memcpy(*host, spec, (size_t)(colon - spec));
	(*host)[colon - spec] = '\0';
	*port = (int)value;
	return 0;
}

static int keycode_from_keysym(rfbKeySym key)
{
	if (key >= 'a' && key <= 'z')
		return KEY_A + (int)(key - 'a');
	if (key >= 'A' && key <= 'Z')
		return KEY_A + (int)(key - 'A');
	if (key >= '1' && key <= '9')
		return KEY_1 + (int)(key - '1');
	if (key == '0')
		return KEY_0;

	switch (key) {
	case XK_Escape:
		return KEY_ESC;
	case XK_Return:
		return KEY_ENTER;
	case XK_Tab:
		return KEY_TAB;
	case XK_BackSpace:
		return KEY_BACKSPACE;
	case XK_space:
		return KEY_SPACE;
	case XK_Control_L:
		return KEY_LEFTCTRL;
	case XK_Control_R:
		return KEY_RIGHTCTRL;
	case XK_Shift_L:
		return KEY_LEFTSHIFT;
	case XK_Shift_R:
		return KEY_RIGHTSHIFT;
	case XK_Alt_L:
		return KEY_LEFTALT;
	case XK_Alt_R:
		return KEY_RIGHTALT;
	case XK_Left:
		return KEY_LEFT;
	case XK_Right:
		return KEY_RIGHT;
	case XK_Up:
		return KEY_UP;
	case XK_Down:
		return KEY_DOWN;
	case XK_Home:
		return KEY_HOME;
	case XK_End:
		return KEY_END;
	case XK_Page_Up:
		return KEY_PAGEUP;
	case XK_Page_Down:
		return KEY_PAGEDOWN;
	case XK_Insert:
		return KEY_INSERT;
	case XK_Delete:
		return KEY_DELETE;
	case XK_F1:
		return KEY_F1;
	case XK_F2:
		return KEY_F2;
	case XK_F3:
		return KEY_F3;
	case XK_F4:
		return KEY_F4;
	case XK_F5:
		return KEY_F5;
	case XK_F6:
		return KEY_F6;
	case XK_F7:
		return KEY_F7;
	case XK_F8:
		return KEY_F8;
	case XK_F9:
		return KEY_F9;
	case XK_F10:
		return KEY_F10;
	case XK_F11:
		return KEY_F11;
	case XK_F12:
		return KEY_F12;
	case XK_minus:
		return KEY_MINUS;
	case XK_equal:
		return KEY_EQUAL;
	case XK_bracketleft:
		return KEY_LEFTBRACE;
	case XK_bracketright:
		return KEY_RIGHTBRACE;
	case XK_backslash:
		return KEY_BACKSLASH;
	case XK_semicolon:
		return KEY_SEMICOLON;
	case XK_apostrophe:
		return KEY_APOSTROPHE;
	case XK_grave:
		return KEY_GRAVE;
	case XK_comma:
		return KEY_COMMA;
	case XK_period:
		return KEY_DOT;
	case XK_slash:
		return KEY_SLASH;
	default:
		return -1;
	}
}

static struct virtio_backend_queue *
input_queue_for_profile(struct virtio_backend_ui_vnc *vnc,
			enum virtio_backend_input_profile profile)
{
	switch (profile) {
	case VIRTIO_BACKEND_INPUT_KEYBOARD:
		return &vnc->keyboardq;
	case VIRTIO_BACKEND_INPUT_MOUSE:
		return &vnc->mouseq;
	default:
		return NULL;
	}
}

static int queue_input_event(struct virtio_backend_ui_vnc *vnc,
			     enum virtio_backend_input_profile profile,
			     uint16_t type, uint16_t code, int32_t value)
{
	struct virtio_backend_queue *queue = input_queue_for_profile(vnc,
								     profile);
	struct virtio_backend_input_event event = {
		.type = type,
		.code = code,
		.value = value,
	};

	if (!queue)
		return -EINVAL;

	return virtio_backend_queue_push(queue, &event, sizeof(event), 0);
}

static void queue_syn(struct virtio_backend_ui_vnc *vnc,
		      enum virtio_backend_input_profile profile)
{
	(void)queue_input_event(vnc, profile, EV_SYN, SYN_REPORT, 0);
}

static void vnc_kbd_event(rfbBool down, rfbKeySym key, rfbClientPtr client)
{
	struct virtio_backend_ui_vnc *vnc = client->screen->screenData;
	int code = keycode_from_keysym(key);

	if (code < 0)
		return;

	(void)queue_input_event(vnc, VIRTIO_BACKEND_INPUT_KEYBOARD, EV_KEY,
				(uint16_t)code, down ? 1 : 0);
	queue_syn(vnc, VIRTIO_BACKEND_INPUT_KEYBOARD);
}

static void queue_button_change(struct virtio_backend_ui_vnc *vnc, int old_mask,
				int new_mask, int bit, uint16_t code)
{
	int old_down = !!(old_mask & bit);
	int new_down = !!(new_mask & bit);

	if (old_down != new_down)
		(void)queue_input_event(vnc, VIRTIO_BACKEND_INPUT_MOUSE,
					EV_KEY, code, new_down ? 1 : 0);
}

static void vnc_ptr_event(int button_mask, int x, int y, rfbClientPtr client)
{
	struct virtio_backend_ui_vnc *vnc = client->screen->screenData;
	int old_buttons = vnc->buttons;
	int dx = 0;
	int dy = 0;

	if (vnc->pointer_initialized) {
		dx = x - vnc->last_x;
		dy = y - vnc->last_y;
	}

	vnc->last_x = x;
	vnc->last_y = y;
	vnc->pointer_initialized = 1;
	vnc->buttons = button_mask;

	if (dx)
		(void)queue_input_event(vnc, VIRTIO_BACKEND_INPUT_MOUSE,
					EV_REL, REL_X, dx);
	if (dy)
		(void)queue_input_event(vnc, VIRTIO_BACKEND_INPUT_MOUSE,
					EV_REL, REL_Y, dy);
	queue_button_change(vnc, old_buttons, button_mask, 1, BTN_LEFT);
	queue_button_change(vnc, old_buttons, button_mask, 2, BTN_MIDDLE);
	queue_button_change(vnc, old_buttons, button_mask, 4, BTN_RIGHT);
	if (button_mask & 8)
		(void)queue_input_event(vnc, VIRTIO_BACKEND_INPUT_MOUSE,
					EV_REL, REL_WHEEL, 1);
	if (button_mask & 16)
		(void)queue_input_event(vnc, VIRTIO_BACKEND_INPUT_MOUSE,
					EV_REL, REL_WHEEL, -1);
	queue_syn(vnc, VIRTIO_BACKEND_INPUT_MOUSE);
}

static int vnc_ui_resize_locked(struct virtio_backend_ui_vnc *vnc,
				uint32_t width, uint32_t height)
{
	uint64_t bytes = (uint64_t)width * height * 4U;
	uint8_t *new_fb;

	if (!width || !height || bytes > SIZE_MAX)
		return -EINVAL;

	if (vnc->width == width && vnc->height == height && vnc->fb)
		return 0;

	new_fb = calloc(1, (size_t)bytes);
	if (!new_fb)
		return -ENOMEM;

	free(vnc->fb);
	vnc->fb = new_fb;
	vnc->width = width;
	vnc->height = height;
	vnc->stride = width * 4U;
	if (vnc->screen) {
		rfbNewFramebuffer(vnc->screen, (char *)vnc->fb, (int)width,
				  (int)height, 8, 3, 4);
	}
	return 0;
}

static int vnc_ui_update(void *opaque, const void *pixels, uint32_t width,
			 uint32_t height, uint32_t stride, uint32_t x,
			 uint32_t y, uint32_t w, uint32_t h)
{
	struct virtio_backend_ui_vnc *vnc = opaque;
	uint32_t row;
	int ret = 0;

	if (!pixels || !width || !height || stride < width * 4U)
		return -EINVAL;

	pthread_mutex_lock(&vnc->lock);
	ret = vnc_ui_resize_locked(vnc, width, height);
	if (ret < 0)
		goto out;

	if (x >= width || y >= height)
		goto out;
	if (x + w > width)
		w = width - x;
	if (y + h > height)
		h = height - y;

	for (row = 0; row < h; row++) {
		const uint8_t *src = (const uint8_t *)pixels +
				     (uint64_t)(y + row) * stride + x * 4U;
		uint8_t *dst = vnc->fb + (uint64_t)(y + row) * vnc->stride +
			       x * 4U;
		memcpy(dst, src, w * 4U);
	}
	if (w && h)
		rfbMarkRectAsModified(vnc->screen, (int)x, (int)y,
				      (int)(x + w), (int)(y + h));

out:
	pthread_mutex_unlock(&vnc->lock);
	return ret;
}

static void vnc_ui_disable(void *opaque)
{
	struct virtio_backend_ui_vnc *vnc = opaque;

	pthread_mutex_lock(&vnc->lock);
	if (vnc->fb) {
		memset(vnc->fb, 0, (size_t)vnc->height * vnc->stride);
		rfbMarkRectAsModified(vnc->screen, 0, 0, (int)vnc->width,
				      (int)vnc->height);
	}
	pthread_mutex_unlock(&vnc->lock);
}

static int vnc_ui_read_input(void *opaque,
			     enum virtio_backend_input_profile profile,
			     struct virtio_backend_input_event *event, int block)
{
	struct virtio_backend_ui_vnc *vnc = opaque;
	struct virtio_backend_queue *queue = input_queue_for_profile(vnc,
								     profile);
	struct virtio_backend_packet *pkt;
	int ret;

	if (!queue || !event)
		return -EINVAL;

	ret = virtio_backend_queue_pop_wait(queue, &pkt, block);
	if (ret < 0)
		return ret;

	if (pkt->len != sizeof(*event)) {
		free(pkt);
		return -EINVAL;
	}

	memcpy(event, pkt->data, sizeof(*event));
	free(pkt);
	return sizeof(*event);
}

static void vnc_ui_destroy(void *opaque)
{
	struct virtio_backend_ui_vnc *vnc = opaque;

	if (!vnc)
		return;
	virtio_backend_queue_stop(&vnc->keyboardq);
	virtio_backend_queue_stop(&vnc->mouseq);
	if (vnc->screen) {
		rfbShutdownServer(vnc->screen, TRUE);
		rfbScreenCleanup(vnc->screen);
	}
	virtio_backend_queue_destroy(&vnc->keyboardq);
	virtio_backend_queue_destroy(&vnc->mouseq);
	if (vnc->lock_initialized)
		pthread_mutex_destroy(&vnc->lock);
	free(vnc->fb);
	free(vnc);
}

static const struct virtio_backend_ui_ops vnc_ui_ops = {
	.update = vnc_ui_update,
	.disable = vnc_ui_disable,
	.read_input = vnc_ui_read_input,
	.destroy = vnc_ui_destroy,
};

int virtio_backend_ui_vnc_create(virtio_backend_ui_handle_t *handle,
				 const char *listen, uint32_t width,
				 uint32_t height)
{
	struct virtio_backend_ui_vnc *vnc;
	char *host = NULL;
	in_addr_t listen_addr;
	int argc = 1;
	char *argv[] = { (char *)"my-virtio-vnc", NULL };
	int port = 0;
	int ret;

	if (!handle)
		return -EINVAL;
	*handle = NULL;

	ret = parse_vnc_listen(listen, &host, &port);
	if (ret < 0)
		return ret;
	if (!rfbStringToAddr(host, &listen_addr)) {
		free(host);
		return -EINVAL;
	}

	vnc = calloc(1, sizeof(*vnc));
	if (!vnc) {
		free(host);
		return -ENOMEM;
	}
	vnc->base.ops = &vnc_ui_ops;
	if (pthread_mutex_init(&vnc->lock, NULL)) {
		ret = -errno;
		goto fail;
	}
	vnc->lock_initialized = 1;
	ret = virtio_backend_queue_init(&vnc->keyboardq,
					VIRTIO_BACKEND_UI_INPUT_QUEUE_DEPTH);
	if (ret < 0)
		goto fail;
	ret = virtio_backend_queue_init(&vnc->mouseq,
					VIRTIO_BACKEND_UI_INPUT_QUEUE_DEPTH);
	if (ret < 0)
		goto fail;

	ret = vnc_ui_resize_locked(vnc, width ? width : 1280,
				   height ? height : 800);
	if (ret < 0)
		goto fail;

	vnc->screen = rfbGetScreen(&argc, argv, (int)vnc->width,
				   (int)vnc->height, 8, 3, 4);
	if (!vnc->screen) {
		ret = -ENOMEM;
		goto fail;
	}
	vnc->screen->screenData = vnc;
	vnc->screen->frameBuffer = (char *)vnc->fb;
	vnc->screen->serverFormat.bitsPerPixel = 32;
	vnc->screen->serverFormat.depth = 24;
	vnc->screen->serverFormat.trueColour = TRUE;
	vnc->screen->serverFormat.redMax = 255;
	vnc->screen->serverFormat.greenMax = 255;
	vnc->screen->serverFormat.blueMax = 255;
	vnc->screen->serverFormat.redShift = 16;
	vnc->screen->serverFormat.greenShift = 8;
	vnc->screen->serverFormat.blueShift = 0;
	vnc->screen->kbdAddEvent = vnc_kbd_event;
	vnc->screen->ptrAddEvent = vnc_ptr_event;
	vnc->screen->alwaysShared = TRUE;
	vnc->screen->autoPort = FALSE;
	vnc->screen->port = port;
	vnc->screen->ipv6port = 0;
	vnc->screen->listenInterface = listen_addr;
	vnc->screen->deferUpdateTime = 5;
	rfbInitServer(vnc->screen);
	rfbRunEventLoop(vnc->screen, 10000, TRUE);

	free(host);
	*handle = vnc;
	return 0;

fail:
	free(host);
	vnc_ui_destroy(vnc);
	return ret;
}
