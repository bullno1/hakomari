#include "ssd1306_gd.h"

int
ssd1306_gd_init(ssd1306_gd_t* image, ssd1306_type_t type)
{
	unsigned int width, height;
	int error;
	if((error = ssd1306_get_dimension(type, &width, &height)) < 0)
	{
		return error;
	}

	gdImagePtr img = gdImageCreate(width, height);
	int clear_color = gdImageColorAllocate(img, 0, 0, 0);
	int draw_color = gdImageColorAllocate(img, 255, 255, 255);

	*image = (ssd1306_gd_t){
		.image = img,
		.clear_color = clear_color,
		.draw_color = draw_color
	};

	return 0;
}

void
ssd1306_gd_cleanup(ssd1306_gd_t* image)
{
	gdImageDestroy(image->image);
}

int
ssd1306_gd_get_pixel(void* image_, unsigned int x, unsigned int y)
{
	ssd1306_gd_t* image = image_;
	return gdImageGetPixel(image->image, x, y) == image->draw_color;
}
