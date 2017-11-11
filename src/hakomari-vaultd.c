#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/random.h>
#include <linux/memfd.h>
#include <ancillary.h>
#include <assert.h>
#include <gdfontt.h>
#include "ssd1306_gd.h"
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)
#define NUM_CHAR_BUTTONS 9
#define NUM_CONTROL_BUTTONS 3
#define HALF_BUTTON_GAP 1

struct char_range_s
{
	char min;
	char max;
};

enum button_type_e
{
	BUTTON_DUMMY,
	BUTTON_SHIFT,
	BUTTON_BACKSPACE,
	BUTTON_RANGE,
	BUTTON_SINGLE
};

struct button_s
{
	enum button_type_e type;

	union
	{
		char single;
		struct char_range_s range;
	} data;
};

struct button_slot_s
{
	unsigned int x;
	unsigned int y;
	unsigned int w;
	unsigned int h;
	struct button_s button;
};

static const struct char_range_s char_ranges[NUM_CHAR_BUTTONS] = {
	{'0', '9'},
	{'a', 'c'},
	{'d', 'f'},
	{'g', 'i'},
	{'j', 'l'},
	{'m', 'o'},
	{'p', 's'},
	{'t', 'v'},
	{'w', 'z'},
};

static void
write_image(gdImagePtr image, int draw_color, uint8_t* image_mem)
{
	for(int y = 0; y < image->sy; ++y)
	{
		for(int x = 0; x < image->sx; x += 8)
		{
			uint8_t byte = 0;

			for(int i = 0; i < 8; ++i)
			{
				uint8_t filled = gdImageGetPixel(image, x + 8 - i, y) == draw_color;
				byte <<= 1;
				byte |= filled;
			}

			int index = (x + y * image->sx) / 8;
			image_mem[index] = byte;
		}
	}
}

static void
draw_label(
	ssd1306_gd_t* fb,
	const struct button_slot_s* button_slot,
	const char* label
)
{
	gdFontPtr font = gdFontGetTiny();
	int len = strlen(label);
	int text_width = len * font->w;
	int text_height = font->h;
	int x_offset = (button_slot->w - text_width) / 2;
	int y_offset = (button_slot->h - text_height) / 2;

	gdImageString(
		fb->image,
		font,
		button_slot->x + x_offset,
		button_slot->y + y_offset,
		(unsigned char*)label,
		fb->draw_color
	);
}

static void
draw_button(ssd1306_gd_t* fb, const struct button_slot_s* button_slot)
{
	gdImageRectangle(
		fb->image,
		button_slot->x, button_slot->y,
		button_slot->x + button_slot->w, button_slot->y + button_slot->h,
		fb->draw_color
	);

	const struct button_s* button = &button_slot->button;
	char label[5];
	switch(button->type)
	{
		case BUTTON_DUMMY:
			break;
		case BUTTON_SHIFT:
			draw_label(fb, button_slot, "^");
			break;
		case BUTTON_BACKSPACE:
			draw_label(fb, button_slot, "<");
			break;
		case BUTTON_RANGE:
			if(button->data.range.min == '0')
			{
				draw_label(fb, button_slot, "0-9");
			}
			else
			{
				for(
					char ch = button->data.range.min;
					ch <= button->data.range.max && (ch - button->data.range.min) < 4;
					++ch
				)
				{
					label[ch - button->data.range.min] = ch;
				}
				label[4] = '\0';

				draw_label(fb, button_slot, label);
			}
			break;
		case BUTTON_SINGLE:
				label[0] = button->data.single;
				label[1] = '\0';
				draw_label(fb, button_slot, label);
			break;
	}
}

static void
draw_buttons(
	ssd1306_gd_t* fb,
	unsigned int num_buttons,
	const struct button_slot_s* button_slot
)
{
	for(unsigned int i = 0; i < num_buttons; ++i)
	{
		draw_button(fb, &button_slot[i]);
	}
}

static int
cmp_max(const void* lhs_, const void* rhs_)
{
	const struct char_range_s* lhs = lhs_;
	const struct char_range_s* rhs = rhs_;

	return lhs->max - rhs->max;
}

static void
randomly_assign_buttons(
	unsigned int num_buttons,
	struct button_slot_s* slots,
	const struct button_s* buttons
)
{
	struct char_range_s button_ranks[num_buttons];

	for(unsigned int i = 0; i < num_buttons; ++i)
	{
		button_ranks[i].min = i;
		getrandom(&button_ranks[i].max, sizeof(button_ranks[i].max), 0);
	}

	qsort(button_ranks, num_buttons, sizeof(button_ranks[0]), cmp_max);

	for(unsigned int i = 0; i < num_buttons; ++i)
	{
		slots[i].button = buttons[(unsigned)button_ranks[i].min];
	}
}

int
main(int argc, const char* argv[])
{
	(void)argc;
	(void)argv;

	int exit_code = EXIT_SUCCESS;

	ssd1306_gd_t fb = { 0 };
	assert(ssd1306_gd_init(&fb, SSD1306_128_64) == 0);
	int memfd = -1;
	uint8_t* image_mem = NULL;

	hakomari_rpc_server_t server;
	hakomari_rpc_client_t client;
	hakomari_rpc_init_server(&server);
	hakomari_rpc_init_client(&client);

	memfd = syscall(__NR_memfd_create, "passphrase-keyboard", MFD_ALLOW_SEALING);
	if(memfd < 0)
	{
		fprintf(stderr, "Error creating memfd: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	size_t length = fb.image->sx * fb.image->sy / 8;
	if(ftruncate(memfd, length) < 0)
	{
		fprintf(stderr, "Error resizing memfd: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	image_mem = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
	if(image_mem == NULL)
	{
		fprintf(stderr, "Error mmap()-ing image fd: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	// Calculate buttons' layout
	const int screen_width = fb.image->sx;
	const int screen_height = fb.image->sy;
	const int text_height = gdFontGetTiny()->h;
	const int buttons_offset = text_height * 2;

	const int button_width = screen_width / 4;
	const int button_height = (screen_height - buttons_offset) / 3;

	struct button_slot_s char_button_slots[NUM_CHAR_BUTTONS];
	struct button_slot_s control_button_slots[NUM_CONTROL_BUTTONS];
	struct button_s char_buttons[NUM_CHAR_BUTTONS];
	struct button_s control_buttons[NUM_CONTROL_BUTTONS];

	for(int i = 0; i < NUM_CHAR_BUTTONS; ++i)
	{
		char_button_slots[i].x = (i % 3) * button_width + HALF_BUTTON_GAP;
		char_button_slots[i].y = buttons_offset + (i / 3) * button_height + HALF_BUTTON_GAP;
		char_button_slots[i].w = button_width - HALF_BUTTON_GAP * 2;
		char_button_slots[i].h = button_height - HALF_BUTTON_GAP * 2;
		char_buttons[i].type = BUTTON_RANGE;
		char_buttons[i].data.range = char_ranges[i];
	}

	for(int i = 0; i < NUM_CONTROL_BUTTONS; ++i)
	{
		control_button_slots[i].x = 3 * button_width + HALF_BUTTON_GAP;
		control_button_slots[i].y = buttons_offset + i * button_height + HALF_BUTTON_GAP;
		control_button_slots[i].w = button_width - HALF_BUTTON_GAP * 2;
		control_button_slots[i].h = button_height - HALF_BUTTON_GAP * 2;
		control_buttons[i].type = (enum button_type_e)i;
	}

	randomly_assign_buttons(NUM_CHAR_BUTTONS, char_button_slots, char_buttons);
	randomly_assign_buttons(NUM_CONTROL_BUTTONS, control_button_slots, control_buttons);

	if(hakomari_rpc_start_client(&client, HAKOMARI_DISPLAYD_SOCK_PATH) != 0)
	{
		fprintf(stderr, "Could not connect to rpc server: %s\n", hakomari_rpc_strerror(&client));
		quit(EXIT_FAILURE);
	}

	hakomari_rpc_req_t* req = hakomari_rpc_begin_req(&client, "stream-image", 0);
	struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
	if(ancil_send_fd(io->fd, memfd) < 0)
	{
		fprintf(stderr, "Error sending fd: %s\n", strerror(errno));
		quit(EXIT_FAILURE);
	}

	gdImageFilledRectangle(fb.image, 0, 0, fb.image->sx - 1, fb.image->sy - 1, fb.clear_color);

	draw_buttons(&fb, NUM_CHAR_BUTTONS, char_button_slots);
	draw_buttons(&fb, NUM_CONTROL_BUTTONS, control_button_slots);

	write_image(fb.image, fb.draw_color, image_mem);

	cmp_write_bool(req->cmp, true);
	fgetc(stdin);

quit:
	hakomari_rpc_stop_client(&client);

	if(image_mem != NULL) { munmap(image_mem, length); }
	if(memfd >= 0) { close(memfd); }

	return exit_code;
}
