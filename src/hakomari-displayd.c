#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/fcntl.h>
#include <unistd.h>
#include <ancillary.h>
#include <c-periphery/gpio.h>
#include <gdfontt.h>
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"
#include "ssd1306.h"
#include "ssd1306_gd.h"

#ifndef HAKOMARI_DISPLAYD_LOCK_FILE
#define HAKOMARI_DISPLAYD_LOCK_FILE HAKOMARI_RUN_PATH "/displayd/lock"
#endif
#ifndef HAKOMARI_DISPLAYD_MAX_MSGLEN
#define HAKOMARI_DISPLAYD_MAX_MSGLEN 1024
#endif
#define quit(code) do { exit_code = code; goto quit; } while(0)

#define POLLGPIO (POLLPRI | POLLERR)

static int
configure_button(gpio_t* gpio, int pin)
{
	int err;
	if((err = gpio_open(gpio, pin, GPIO_DIR_IN)) != 0)
	{
		return err;
	}

	if((err = gpio_set_edge(gpio, GPIO_EDGE_RISING)) != 0)
	{
		return err;
	}

	return 0;
}

static int
show_text(ssd1306_gd_t* fb, ssd1306_t* display, const char* text)
{
	gdFontPtr font = gdFontGetTiny();

	gdImageFilledRectangle(
		fb->image,
		0, 0, fb->image->sx - 1, fb->image->sy -1,
		fb->clear_color
	);

	unsigned int x, y;
	x = y = 0;

	for(unsigned int i = 0; text[i] != '\0'; ++i)
	{
		char ch = text[i];
		switch(ch)
		{
			case '\n':
				x = 0;
				y += font->h;
				break;
			case '\t':
				x += font->w * 4;
				break;
			default:
				gdImageChar(fb->image, font, x, y, ch, fb->draw_color);
				x += font->w;
				if((int)(x + font->w) > (int)fb->image->sx)
				{
					x = 0;
					y += font->h;
				}
				break;
		}
	}

	int err;
	if((err = ssd1306_gd_display(fb, display)) != 0)
	{
		fprintf(stderr, "Error sending image: %s\n", ssd1306_error(display));
		return err;
	}

	return 0;
}

int
main(int argc, const char* argv[])
{
	setvbuf(stdout, NULL, _IOLBF, 1024);
	setvbuf(stderr, NULL, _IOLBF, 1024);

	int exit_code = EXIT_SUCCESS;
	bool display_initialized = false;
	hakomari_rpc_server_t rpc;
	hakomari_rpc_init_server(&rpc);
	ssd1306_gd_t fb = { 0 };
	assert(ssd1306_gd_init(&fb, SSD1306_128_64) == 0);

	gpio_t accept_button, reject_button;
	accept_button = reject_button = (gpio_t){ .fd = -1 };

	if(argc != 2)
	{
		fprintf(stderr, "Usage: hakomari-displayd <i2c-dev>\n");
		quit(EXIT_FAILURE);
	}

	fprintf(stdout, "Starting RPC server\n");
	if(hakomari_rpc_start_server(
		&rpc, HAKOMARI_DISPLAYD_SOCK_PATH, HAKOMARI_DISPLAYD_LOCK_FILE
	) != 0)
	{
		fprintf(stderr, "Could not start rpc server: %s\n", hakomari_rpc_strerror(&rpc));
		quit(EXIT_FAILURE);
	}

	fprintf(stdout, "Configuring buttons\n");
	if(configure_button(&accept_button, 6) != 0)
	{
		fprintf(stderr, "Could not configure button: %s\n", gpio_errmsg(&accept_button));
		quit(EXIT_FAILURE);
	}

	if(configure_button(&reject_button, 5) != 0)
	{
		fprintf(stderr, "Could not configure button: %s\n", gpio_errmsg(&reject_button));
		quit(EXIT_FAILURE);
	}

	fprintf(stdout, "Connecting to display\n");
	ssd1306_t display;
	if(ssd1306_init(&display, argv[1], SSD1306_128_64) != 0)
	{
		fprintf(stderr, "Error connecting to display: %s\n", ssd1306_error(&display));
		quit(EXIT_FAILURE);
	}

	display_initialized = true;

	fprintf(stdout, "Initializing display\n");
	if(ssd1306_begin(&display, SSD1306_SWITCHCAPVCC) != 0)
	{
		fprintf(stderr, "Error initializing display: %s\n", ssd1306_error(&display));
		quit(EXIT_FAILURE);
	}

	if(show_text(&fb, &display, "Hakomari ready!") != 0) { quit(EXIT_FAILURE); }

	while(true)
	{
		hakomari_rpc_req_t* req = hakomari_rpc_next_req(&rpc);
		if(req == NULL)
		{
			if(rpc.stopping)
			{
				fprintf(stdout, "Shutting down\n");
				quit(EXIT_SUCCESS);
			}
			else
			{
				fprintf(stderr, "Error getting next request: %s\n", hakomari_rpc_strerror(&rpc));
				quit(EXIT_FAILURE);
			}
		}

		if(
			(strcmp(req->method, "show") == 0 || strcmp(req->method, "confirm") == 0)
			&& req->num_args == 1
		)
		{
			hakomari_rpc_string_t msg;
			uint32_t size = sizeof(msg);
			if(!cmp_read_str(req->cmp, msg, &size))
			{
				continue;
			}

			fprintf(stdout, "%s(\"%s\")\n", req->method, msg);

			if(show_text(&fb, &display, msg) != 0)
			{
				hakomari_rpc_reply_error(req, "server-error");
				quit(EXIT_FAILURE);
			}

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}

			if(strcmp(req->method, "show") == 0)
			{
				if(!cmp_write_bool(req->cmp, true))
				{
					fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
					continue;
				}
			}
			else
			{
				char ch;
				if(false
					|| read(gpio_fd(&accept_button), &ch, sizeof(ch)) < 0
					|| lseek(gpio_fd(&accept_button), 0, SEEK_END) < 0
				)
				{
					fprintf(stderr, "Error polling accept buttons: %s\n", strerror(errno));
					continue;
				}

				if(false
					|| read(gpio_fd(&reject_button), &ch, sizeof(ch)) < 0
					|| lseek(gpio_fd(&reject_button), 0, SEEK_END) < 0
				)
				{
					fprintf(stderr, "Error polling reject buttons: %s\n", strerror(errno));
					continue;
				}

				struct pollfd pfds[2];
				pfds[0].fd = gpio_fd(&accept_button);
				pfds[0].events = POLLGPIO;
				pfds[1].fd = gpio_fd(&reject_button);
				pfds[1].events = POLLGPIO;

				if(poll(pfds, 2, HAKOMARI_RPC_TIMEOUT) < 0)
				{
					fprintf(stderr, "Error polling buttons: %s\n", strerror(errno));
					continue;
				}

				if(lseek(gpio_fd(&accept_button), 0, SEEK_SET) < 0)
				{
					fprintf(stderr, "Error polling accept buttons: %s\n", strerror(errno));
					continue;
				}

				if(lseek(gpio_fd(&reject_button), 0, SEEK_SET) < 0)
				{
					fprintf(stderr, "Error polling reject buttons: %s\n", strerror(errno));
					continue;
				}

				bool accept_button_released = (pfds[0].revents & POLLGPIO) > 0;
				bool reject_button_up = false;
				if(gpio_read(&reject_button, &reject_button_up) != 0)
				{
					fprintf(stderr, "Error polling reject buttons: %s\n", gpio_errmsg(&reject_button));
					continue;
				}

				bool accepted = accept_button_released && reject_button_up;

				if(!cmp_write_bool(req->cmp, accepted))
				{
					fprintf(stderr, "Error sending result: %s\n", cmp_strerror(req->cmp));
					continue;
				}

				const char* decision = accepted ? "accepted" : "rejected";
				fprintf(stdout, "%s\n", decision);

				if(show_text(&fb, &display, decision) != 0)
				{
					quit(EXIT_FAILURE);
				}
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}
		}
		else if(
			strcmp(req->method, "stream-image") == 0 && req->num_args == 0
		)
		{
			int image_fd = -1;
			uint8_t* image_mem = NULL;
			size_t length = fb.image->sx * fb.image->sy / 8;

			struct hakomari_rpc_io_ctx_s* io = req->cmp->buf;
			if(ancil_recv_fd(io->fd, &image_fd) < 0)
			{
				fprintf(stderr, "Error receiving fd: %s\n", strerror(errno));
				goto end_stream_image;
			}

			fprintf(stdout, "Received fd: %d\n", image_fd);

			// Including fcntl.h and linux/fcntl.h at the same time results in
			// redefinition error so make the syscall ourselves
			int seals = syscall(__NR_fcntl, image_fd, F_GET_SEALS);
			if(seals < 0)
			{
				fprintf(stderr, "Error geting seal: %s\n", strerror(errno));
				goto end_stream_image;
			}

			if((seals & F_SEAL_SHRINK) == 0)
			{
				fprintf(stderr, "Image file does not have the correct seal\n");
				goto end_stream_image;
			}

			off_t file_size = lseek(image_fd, 0, SEEK_END);
			if(file_size < 0)
			{
				fprintf(stderr, "lseek() failed: %s\n", strerror(errno));
				goto end_stream_image;
			}

			if(file_size < (off_t)length)
			{
				fprintf(stderr, "Image file is not big enough\n");
				goto end_stream_image;
			}

			image_mem = mmap(NULL, length, PROT_READ, MAP_SHARED, image_fd, 0);
			if(image_mem == NULL)
			{
				fprintf(stderr, "Error mmap()-ing image fd: %s\n", strerror(errno));
				goto end_stream_image;
			}

			while(true)
			{
				bool show_next;
				if(!cmp_read_bool(req->cmp, &show_next) || !show_next)
				{
					goto end_stream_image;
				}

				gdImageFilledRectangle(
					fb.image,
					0, 0, fb.image->sx - 1, fb.image->sy -1,
					fb.clear_color
				);

				int colors[] = { fb.clear_color, fb.draw_color };
				for(size_t i = 0; i < length; ++i)
				{
					uint8_t byte = image_mem[i];
					int x = (i * 8) % fb.image->sx;
					int y = (i * 8) / fb.image->sx;
					for(unsigned int j = 0; j < 8; ++j)
					{
						int color = colors[byte & 1];
						gdImageSetPixel(fb.image, x + j, y, color);
						byte >>= 1;
					}
				}

				if(ssd1306_gd_display(&fb, &display) != 0)
				{
					fprintf(stderr, "Error sending image: %s\n", ssd1306_error(&display));
					goto end_stream_image;
				}
			}

end_stream_image:
			show_text(&fb, &display, "");
			if(image_mem != NULL) { munmap(image_mem, length); }
			if(image_fd >= 0) { close(image_fd); }
		}
		else
		{
			hakomari_rpc_reply_error(req, "invalid-method");
		}
	}

quit:
	gpio_close(&accept_button);
	gpio_close(&reject_button);

	if(display_initialized)
	{
		show_text(&fb, &display, "");
		ssd1306_end(&display);
		ssd1306_cleanup(&display);
	}
	ssd1306_gd_cleanup(&fb);
	hakomari_rpc_stop_server(&rpc);

	return exit_code;
}
