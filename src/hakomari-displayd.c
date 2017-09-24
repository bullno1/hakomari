#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <assert.h>
#include "ssd1306.h"
#include "ssd1306_gd.h"

#define quit(code) do { exit_code = code; goto quit; } while(0)

int
main(int argc, const char* argv[])
{
	int exit_code = EXIT_SUCCESS;
	bool display_initialized = false;
	ssd1306_gd_t fb = { 0 };
	assert(ssd1306_gd_init(&fb, SSD1306_128_64) == 0);

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

	gdImageFill(fb.image, 0, 0, fb.clear_color);
	gdImageLine(fb.image, 0, 0, 128, 64, fb.draw_color);
	gdImageFilledEllipse(fb.image, 80, 30, 30, 30, fb.draw_color);
	ssd1306_gd_display(&fb, &display);

	char ch;
	fread(&ch, sizeof(ch), 1, stdin);

quit:
	if(display_initialized)
	{
		ssd1306_end(&display);
		ssd1306_cleanup(&display);
	}
	ssd1306_gd_cleanup(&fb);

	return EXIT_SUCCESS;
}
