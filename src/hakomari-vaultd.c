#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <ancillary.h>
#include <assert.h>
#include "ssd1306_gd.h"
#include "hakomari-cfg.h"
#include "hakomari-rpc.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

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
	gdImageDashedLine(fb.image, 0, 0, fb.image->sx - 1, fb.image->sy - 1, fb.draw_color);
	gdImageFilledEllipse(fb.image, fb.image->sx / 2, fb.image->sx / 3, 10, 20, fb.draw_color);

	for(int y = 0; y < fb.image->sy; ++y)
	{
		for(int x = 0; x < fb.image->sx; x += 8)
		{
			uint8_t byte = 0;

			for(int i = 0; i < 8; ++i)
			{
				uint8_t filled = gdImageGetPixel(fb.image, x + 8 - i, y) == fb.draw_color;
				byte <<= 1;
				byte |= filled;
			}

			int index = (x + y * fb.image->sx) / 8;
			image_mem[index] = byte;
		}
	}

	cmp_write_bool(req->cmp, true);

	fgetc(stdin);

quit:
	hakomari_rpc_stop_client(&client);

	if(image_mem != NULL) { munmap(image_mem, length); }
	if(memfd >= 0) { close(memfd); }

	return exit_code;
}
