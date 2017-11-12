#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

// Single entry for now
typedef struct passphrase_entry_s passphrase_cache_t;

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

struct passphrase_entry_s
{
	hakomari_rpc_string_t endpoint;
	hakomari_rpc_string_t passphrase;
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

static const char symbols[] = "_!@#$%^&*()";

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
				uint8_t filled = gdImageGetPixel(image, x + 7 - i, y) == draw_color;
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
draw_button(ssd1306_gd_t* fb, bool shift, const struct button_slot_s* button_slot)
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
			draw_label(fb, button_slot, shift ? "SHIFT" : "shift");
			break;
		case BUTTON_BACKSPACE:
			draw_label(fb, button_slot, "del");
			break;
		case BUTTON_RANGE:
			if(button->data.range.min == '0')
			{
				draw_label(fb, button_slot,  shift ? "!@#$" : "0-9");
			}
			else
			{
				for(
					char ch = button->data.range.min;
					ch <= button->data.range.max && (ch - button->data.range.min) < 4;
					++ch
				)
				{
					label[ch - button->data.range.min] = shift ? toupper(ch) : ch;
				}
				label[button->data.range.max - button->data.range.min + 1] = '\0';

				draw_label(fb, button_slot, label);
			}
			break;
		case BUTTON_SINGLE:
			if(isdigit(button->data.single))
			{
				label[0] = shift ? symbols[button->data.single - '0'] : button->data.single;
			}
			else
			{
				label[0] = shift ? toupper(button->data.single) : button->data.single;
			}
			label[1] = '\0';
			draw_label(fb, button_slot, label);
			break;
	}
}

static void
draw_buttons(
	ssd1306_gd_t* fb,
	bool shift,
	unsigned int num_buttons,
	const struct button_slot_s* button_slot
)
{
	for(unsigned int i = 0; i < num_buttons; ++i)
	{
		draw_button(fb, shift, &button_slot[i]);
	}
}

static void
draw_text_wrapped(
	ssd1306_gd_t* fb,
	int x, int y,
	gdFontPtr font,
	const char* string
)
{
	gdImageString(fb->image, font, x, y, (unsigned char*)string, fb->draw_color);
	/*int char_x = x;*/
	/*int char_y = y;*/
	/*for(const char* ch = string; *ch != '\0'; ++ch)*/
	/*{*/
		/*gdImageChar(fb->image, font, char_x, char_y, fb->draw_color, (unsigned char)*ch);*/
		/*char_x += font->w;*/
		/*if(char_x > fb->image->sx)*/
		/*{*/
			/*char_x = x;*/
			/*char_y += font->h;*/
		/*}*/
	/*}*/
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

static void
init_cache(passphrase_cache_t* cache)
{
	memset(cache, 0, sizeof(*cache));
}

static bool
get_cache(
	passphrase_cache_t* cache, hakomari_rpc_string_t key, hakomari_rpc_string_t value)
{
	if(strcmp(cache->endpoint, key) != 0) { return false; }

	strncpy(value, cache->passphrase, strlen(cache->passphrase));
	return true;
}

static bool
evict_cache(passphrase_cache_t* cache, hakomari_rpc_string_t key)
{
	if(strcmp(cache->endpoint, key) != 0) { return false; }

	init_cache(cache);
	return true;
}


static void
put_cache(
	passphrase_cache_t* cache, hakomari_rpc_string_t key, hakomari_rpc_string_t value)
{
	memcpy(cache->endpoint, key, sizeof(hakomari_rpc_string_t));
	memcpy(cache->passphrase, value, sizeof(hakomari_rpc_string_t));
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

	if(argc != 3)
	{
		fprintf(stderr, "Usage: hakomari-vaultd <sock-path> <lock-path>\n");
		quit(EXIT_FAILURE);
	}

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

	struct button_slot_s button_slots[NUM_CHAR_BUTTONS + NUM_CONTROL_BUTTONS];
	struct button_s char_buttons[NUM_CHAR_BUTTONS];
	struct button_s control_buttons[NUM_CONTROL_BUTTONS];
	struct button_s single_buttons[NUM_CHAR_BUTTONS];

	for(int i = 0; i < NUM_CHAR_BUTTONS; ++i)
	{
		struct button_slot_s* button_slot = &button_slots[i];
		button_slot->x = (i % 3) * button_width + HALF_BUTTON_GAP;
		button_slot->y = buttons_offset + (i / 3) * button_height + HALF_BUTTON_GAP;
		button_slot->w = button_width - HALF_BUTTON_GAP * 2;
		button_slot->h = button_height - HALF_BUTTON_GAP * 2;
		char_buttons[i].type = BUTTON_RANGE;
		char_buttons[i].data.range = char_ranges[i];
	}

	for(int i = 0; i < NUM_CONTROL_BUTTONS; ++i)
	{
		struct button_slot_s* button_slot = &button_slots[i + NUM_CHAR_BUTTONS];
		button_slot->x = 3 * button_width + HALF_BUTTON_GAP;
		button_slot->y = buttons_offset + i * button_height + HALF_BUTTON_GAP;
		button_slot->w = button_width - HALF_BUTTON_GAP * 2;
		button_slot->h = button_height - HALF_BUTTON_GAP * 2;
		control_buttons[i].type = (enum button_type_e)i;
	}

	if(hakomari_rpc_start_server(&server, argv[1], argv[2]) != 0)
	{
		fprintf(stderr, "Error starting RPC server: %s\n", hakomari_rpc_strerror(&server));
		quit(EXIT_FAILURE);
	}

	struct passphrase_entry_s cache;
	init_cache(&cache);

	while(true)
	{
		hakomari_rpc_req_t* req = hakomari_rpc_next_req(&server);
		if(req == NULL)
		{
			if(server.stopping)
			{
				fprintf(stdout, "Shutting down\n");
				quit(EXIT_SUCCESS);
			}
			else
			{
				fprintf(stderr, "Error getting next request: %s\n", hakomari_rpc_strerror(&server));
				quit(EXIT_FAILURE);
			}
		}

		if(strcmp(req->method, "get-passphrase-screen") == 0 && req->num_args == 1)
		{
			if(!cmp_skip_object(req->cmp, NULL))
			{
				fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			fprintf(stdout, "%s()\n", req->method);

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				continue;
			}

			unsigned int num_buttons = NUM_CHAR_BUTTONS + NUM_CONTROL_BUTTONS;
			if(false
				|| !cmp_write_map(req->cmp, 3)
				|| !cmp_write_str(req->cmp, "width", sizeof("width") - 1)
				|| !cmp_write_uint(req->cmp, (unsigned int)fb.image->sx)
				|| !cmp_write_str(req->cmp, "height", sizeof("height") - 1)
				|| !cmp_write_uint(req->cmp, (unsigned int)fb.image->sy)
				|| !cmp_write_str(req->cmp, "buttons", sizeof("buttons") - 1)
				|| !cmp_write_array(req->cmp, num_buttons)
			)
			{
				fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			for(unsigned int i = 0; i < NUM_CHAR_BUTTONS + NUM_CONTROL_BUTTONS; ++i)
			{
				const struct button_slot_s* button = &button_slots[i];
				if(false
					|| !cmp_write_map(req->cmp, 4)
					|| !cmp_write_str(req->cmp, "x", 1)
					|| !cmp_write_uint(req->cmp, button->x)
					|| !cmp_write_str(req->cmp, "y", 1)
					|| !cmp_write_uint(req->cmp, button->y)
					|| !cmp_write_str(req->cmp, "w", 1)
					|| !cmp_write_uint(req->cmp, button->w)
					|| !cmp_write_str(req->cmp, "h", 1)
					|| !cmp_write_uint(req->cmp, button->h)
				)
				{
					fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
					continue;
				}
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				continue;
			}
		}
		else if(strcmp(req->method, "ask-passphrase") == 0 && req->num_args == 1)
		{
			hakomari_rpc_string_t endpoint, passphrase;

			uint32_t size = sizeof(endpoint);
			if(!cmp_read_str(req->cmp, endpoint, &size))
			{
				fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			fprintf(stdout, "%s(%s)\n", req->method, endpoint);

			bool has_passphrase = get_cache(&cache, endpoint, passphrase);

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				continue;
			}

			if(false
				|| (has_passphrase && !cmp_write_str(req->cmp, passphrase, strlen(passphrase)))
				|| (!has_passphrase && !cmp_write_nil(req->cmp))
			)
			{
				fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				continue;
			}
		}
		else if(strcmp(req->method, "forget-passphrase") == 0 && req->num_args == 1)
		{
			hakomari_rpc_string_t endpoint;

			uint32_t size = sizeof(endpoint);
			if(!cmp_read_str(req->cmp, endpoint, &size))
			{
				fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			fprintf(stdout, "%s(%s)\n", req->method, endpoint);

			evict_cache(&cache, endpoint);

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				continue;
			}

			if(!cmp_write_nil(req->cmp))
			{
				fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				continue;
			}
		}
		else if(strcmp(req->method, "input-passphrase") == 0 && req->num_args == 1)
		{
			hakomari_rpc_string_t endpoint;

			uint32_t size = sizeof(endpoint);
			if(!cmp_read_str(req->cmp, endpoint, &size))
			{
				fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
				continue;
			}

			fprintf(stdout, "%s(%s)\n", req->method, endpoint);

			randomly_assign_buttons(
				NUM_CHAR_BUTTONS, button_slots, char_buttons
			);
			randomly_assign_buttons(
				NUM_CONTROL_BUTTONS, button_slots + NUM_CHAR_BUTTONS, control_buttons
			);

			if(hakomari_rpc_start_client(&client, HAKOMARI_DISPLAYD_SOCK_PATH) != 0)
			{
				fprintf(stderr, "Could not connect to rpc server: %s\n", hakomari_rpc_strerror(&client));
				continue;
			}

			hakomari_rpc_req_t* stream_req = hakomari_rpc_begin_req(&client, "stream-image", 0);
			struct hakomari_rpc_io_ctx_s* io = stream_req->cmp->buf;
			if(ancil_send_fd(io->fd, memfd) < 0)
			{
				fprintf(stderr, "Error sending fd: %s\n", strerror(errno));
				goto end_input_passphrase;
			}

			hakomari_rpc_string_t passphrase = { 0 };
			unsigned int passphrase_length = 0;
			bool reading_input = true;
			uint32_t cursor_x = 0;
			uint32_t cursor_y = 0;
			bool shift = false;
			while(reading_input && passphrase_length < sizeof(passphrase) - 1)
			{
				gdImageFilledRectangle(
					fb.image,
					0, 0, fb.image->sx - 1, fb.image->sy - 1,
					fb.clear_color
				);

				draw_text_wrapped(&fb, 1, 0, gdFontGetTiny(), passphrase);

				draw_buttons(
					&fb, shift,
					NUM_CHAR_BUTTONS + NUM_CONTROL_BUTTONS, button_slots
				);

				gdImageChar(
					fb.image,
					gdFontGetTiny(),
					cursor_x, cursor_y,
					(unsigned char)'+',
					fb.draw_color
				);

				write_image(fb.image, fb.draw_color, image_mem);

				if(!cmp_write_bool(stream_req->cmp, true))
				{
					fprintf(stderr, "Error streaming image: %s\n", cmp_strerror(stream_req->cmp));
					goto end_input_passphrase;
				}

				cmp_object_t obj;
				if(!cmp_read_object(req->cmp, &obj))
				{
					fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
					goto end_input_passphrase;
				}

				switch(obj.type)
				{
					case CMP_TYPE_NIL:
						reading_input = false;
						continue;
					case CMP_TYPE_ARRAY16:
					case CMP_TYPE_ARRAY32:
					case CMP_TYPE_FIXARRAY:
						if(obj.as.array_size != 3)
						{
							fprintf(stderr, "Error reading request: Format error\n");
							goto end_input_passphrase;
						}
						break;
					default:
						fprintf(stderr, "Format error: %d\n", obj.type);
						goto end_input_passphrase;
				}

				bool down;
				if(false
					|| !cmp_read_uint(req->cmp, &cursor_x)
					|| !cmp_read_uint(req->cmp, &cursor_y)
					|| !cmp_read_bool(req->cmp, &down)
				)
				{
					fprintf(stderr, "Error reading request: %s\n", cmp_strerror(req->cmp));
					goto end_input_passphrase;
				}

				if(!down) { continue; }

				struct button_s* clicked_button = NULL;
				for(unsigned int i = 0; i < NUM_CHAR_BUTTONS + NUM_CONTROL_BUTTONS; ++i)
				{
					struct button_slot_s* button_slot = &button_slots[i];
					if(true
						&& button_slot->x < cursor_x
						&& cursor_x < button_slot->x + button_slot->w
						&& button_slot->y < cursor_y
						&& cursor_y < button_slot->y + button_slot->h
					)
					{
						clicked_button = &button_slot->button;
						break;
					}
				}

				if(clicked_button == NULL) { continue; }

				bool shuffle_chars = false;
				bool shuffle_controls = false;
				switch(clicked_button->type)
				{
					case BUTTON_DUMMY:
						shuffle_controls = true;
						shuffle_chars = true;
						break;
					case BUTTON_SHIFT:
						shift = !shift;
						shuffle_controls = true;
						break;
					case BUTTON_BACKSPACE:
						if(passphrase_length > 0) { --passphrase_length; }
						passphrase[passphrase_length] = '\0';
						shuffle_controls = true;
						shuffle_chars = true;
						break;
					case BUTTON_RANGE:
						{
							char min = clicked_button->data.range.min;
							char max = clicked_button->data.range.max;
							char range = max - min;

							for(unsigned int i = 0; i < NUM_CHAR_BUTTONS; ++i)
							{
								if(i <= (unsigned char)range)
								{
									single_buttons[i].type = BUTTON_SINGLE;
									single_buttons[i].data.single = min + i;
								}
								else
								{
									single_buttons[i].type = BUTTON_DUMMY;
								}

								randomly_assign_buttons(
									NUM_CHAR_BUTTONS, button_slots, single_buttons
								);
							}
						}
						break;
					case BUTTON_SINGLE:
						{
							char new_char = 0;
							if(isdigit(clicked_button->data.single))
							{
								new_char = shift
									? symbols[clicked_button->data.single - '0']
									: clicked_button->data.single;
							}
							else
							{
								new_char = shift
									? toupper(clicked_button->data.single)
									: clicked_button->data.single;
							}

							passphrase[passphrase_length++] = new_char;
							passphrase[passphrase_length] = '\0';
						}
						shuffle_chars = true;
						shuffle_controls = true;
						break;
				}

				if(shuffle_chars)
				{
					randomly_assign_buttons(
						NUM_CHAR_BUTTONS, button_slots, char_buttons
					);
				}

				if(shuffle_controls)
				{
					randomly_assign_buttons(
						NUM_CONTROL_BUTTONS, button_slots + NUM_CHAR_BUTTONS, control_buttons
					);
				}
			}

			passphrase[passphrase_length] = 0;
			put_cache(&cache, endpoint, passphrase);

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				goto end_input_passphrase;
			}

			if(!cmp_write_u8(req->cmp, HAKOMARI_OK))
			{
				fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&server));
				goto end_input_passphrase;
			}

end_input_passphrase:
			cmp_write_bool(stream_req->cmp, false);
			hakomari_rpc_stop_client(&client);
		}
		else
		{
			hakomari_rpc_reply_error(req, "invalid-method");
		}
	}

quit:
	hakomari_rpc_stop_client(&client);

	if(image_mem != NULL) { munmap(image_mem, length); }
	if(memfd >= 0) { close(memfd); }

	return exit_code;
}
