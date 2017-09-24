#ifndef SSD1306_GD_H
#define SSD1306_GD_H

#include <gd.h>
#include "ssd1306.h"

typedef struct ssd1306_gd_s ssd1306_gd_t;

struct ssd1306_gd_s
{
	gdImagePtr image;
	int draw_color;
	int clear_color;
};

int
ssd1306_gd_init(ssd1306_gd_t* image, ssd1306_type_t type);

void
ssd1306_gd_cleanup(ssd1306_gd_t* image);

int
ssd1306_gd_get_pixel(void* image, unsigned int x, unsigned int y);

static inline int
ssd1306_gd_display(ssd1306_gd_t* image, ssd1306_t* display)
{
	return ssd1306_display(display, image, ssd1306_gd_get_pixel);
}

#endif
