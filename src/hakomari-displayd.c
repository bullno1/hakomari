#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "ssd1306.h"

int
main(int argc, const char* argv[])
{
	if(argc != 2)
	{
		fprintf(stderr, "Usage: hakomari-displayd <i2c-dev>\n");
		return EXIT_FAILURE;
	}

	ssd1306_t display;
	if(ssd1306_init(&display, argv[1], SSD1306_128_64) < 0)
	{
		fprintf(stderr, "Error connecting to display: %s\n", ssd1306_error(&display));
		return EXIT_FAILURE;
	}

	if(ssd1306_begin(&display, SSD1306_SWITCHCAPVCC) < 0)
	{
		fprintf(stderr, "Error connecting to display: %s\n", ssd1306_error(&display));
		ssd1306_cleanup(&display);
		return EXIT_FAILURE;
	}

	size_t size;
	ssd1306_get_buf_size(SSD1306_128_64, &size);

	uint8_t buf[size];
	memset(buf, 0xFF, size);

	if(ssd1306_display(&display, buf) < 0)
	{
		fprintf(stderr, "Error sending image to display: %s\n", ssd1306_error(&display));
		ssd1306_cleanup(&display);
		return EXIT_FAILURE;
	}

	ssd1306_cleanup(&display);

	return EXIT_SUCCESS;
}
