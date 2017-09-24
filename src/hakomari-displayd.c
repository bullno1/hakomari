#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <gd.h>
#include "ssd1306.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

static void
copy_gdimage_to_ssd1306_buf(
	gdImagePtr image, uint8_t* buf, int fillColor,
	unsigned int x, unsigned int y, unsigned int width, unsigned int height
)
{
	memset(buf, 0, width * height / 8);

	for(unsigned int y_offset = 0; y_offset < height; ++y_offset)
	{
		for(unsigned int x_offset = 0; x_offset < width; ++x_offset)
		{
			if(gdImageGetPixel(image, x + x_offset, y + y_offset) == fillColor)
			{
				unsigned int byte_offset = (y_offset / 8) * width + x_offset;
				unsigned int bit_offset = y_offset % 8;
				uint8_t mask = 1 << bit_offset;
				buf[byte_offset] |= mask;
			}
		}
	}
}


int
main(int argc, const char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	bool display_initialized = false;

	uint8_t width, height;
	ssd1306_get_dimension(SSD1306_128_64, &width, &height);
	gdImagePtr image = gdImageCreate(width, height);
	int black = gdImageColorAllocate(image, 0, 0, 0);
	int white = gdImageColorAllocate(image, 255, 255, 255);
	gdImageFilledRectangle(image, 0, 0, width, height, black);
	gdImageLine(image, 0, 0, width - 1 , height - 1, white);
	gdImageEllipse(image, 30, 40, 50, 60, white);

	if(argc != 2)
	{
		fprintf(stderr, "Usage: hakomari-displayd <i2c-dev>\n");
		quit(EXIT_FAILURE);
	}

	ssd1306_t display;
	if(ssd1306_init(&display, argv[1], SSD1306_128_64) < 0)
	{
		fprintf(stderr, "Error connecting to display: %s\n", ssd1306_error(&display));
		quit(EXIT_FAILURE);
	}

	display_initialized = true;

	if(ssd1306_begin(&display, SSD1306_SWITCHCAPVCC) < 0)
	{
		fprintf(stderr, "Error initializing display: %s\n", ssd1306_error(&display));
		quit(EXIT_FAILURE);
	}

	size_t size;
	ssd1306_get_buf_size(SSD1306_128_64, &size);

	{
		uint8_t buf[size];
		memset(buf, 1, size);
		copy_gdimage_to_ssd1306_buf(image, buf, white, 0, 0, width, height);

		if(ssd1306_display(&display, buf) < 0)
		{
			fprintf(stderr, "Error sending image to display: %s\n", ssd1306_error(&display));
			quit(EXIT_FAILURE);
		}
	}

	char ch;
	fread(&ch, sizeof(ch), 1, stdin);

quit:
	if(display_initialized)
	{
		ssd1306_end(&display);
		ssd1306_cleanup(&display);
	}
	gdImageDestroy(image);

	return EXIT_SUCCESS;
}
