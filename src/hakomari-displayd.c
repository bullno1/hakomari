#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <gdfontt.h>
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"
#include "ssd1306.h"
#include "ssd1306_gd.h"

#ifndef HAKOMARI_DISPLAYD_LOCK_FILE
#define HAKOMARI_DISPLAYD_LOCK_FILE HAKOMARI_RUN_PATH "/displayd.lock"
#endif
#ifndef HAKOMARI_DISPLAYD_MAX_MSGLEN
#define HAKOMARI_DISPLAYD_MAX_MSGLEN 1024
#endif
#define quit(code) do { exit_code = code; goto quit; } while(0)

int
main(int argc, const char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	bool display_initialized = false;
	hakomari_rpc_server_t rpc;
	hakomari_rpc_init_server(&rpc);
	ssd1306_gd_t fb = { 0 };
	assert(ssd1306_gd_init(&fb, SSD1306_128_64) == 0);

	unsigned int width, height;
	assert(ssd1306_get_dimension(SSD1306_128_64, &width, &height) == 0);

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
				hakomari_rpc_reply_error(req, "invalid-arg");
				continue;
			}

			fprintf(stdout, "%s(\"%s\")\n", req->method, msg);

			gdImageFilledRectangle(
				fb.image,
				0, 0, width - 1, height -1,
				fb.clear_color
			);
			gdImageString(fb.image, gdFontGetTiny(), 0, 0, (unsigned char*)msg, fb.draw_color);
			if(ssd1306_gd_display(&fb, &display) != 0)
			{
				fprintf(stderr, "Error sending image: %s\n", ssd1306_error(&display));
				hakomari_rpc_reply_error(req, "server-error");
				quit(EXIT_FAILURE);
			}

			if(hakomari_rpc_begin_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}

			if(!cmp_write_nil(req->cmp))
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}

			if(hakomari_rpc_end_result(req) != 0)
			{
				fprintf(stderr, "Error sending result: %s\n", hakomari_rpc_strerror(&rpc));
				continue;
			}
		}
		else
		{
			hakomari_rpc_reply_error(req, "invalid-method");
		}
	}

quit:
	if(display_initialized)
	{
		gdImageFilledRectangle(
			fb.image,
			0, 0, width - 1, height -1,
			fb.clear_color
		);
		ssd1306_gd_display(&fb, &display);

		ssd1306_end(&display);
		ssd1306_cleanup(&display);
	}
	ssd1306_gd_cleanup(&fb);
	hakomari_rpc_stop_server(&rpc);

	return exit_code;
}
