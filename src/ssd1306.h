#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <c-periphery/i2c.h>

#define SSD1306_EXTERNALVCC 0x1
#define SSD1306_SWITCHCAPVCC 0x2

typedef struct ssd1306_s ssd1306_t;

typedef enum ssd1306_type_e
{
	SSD1306_128_64
} ssd1306_type_t;

struct ssd1306_s
{
	ssd1306_type_t type;
	i2c_t i2c;
	uint8_t vccstate;
	int error;
};

typedef int ssd1306_get_pixel_fn(void* image, unsigned int x, unsigned int y);

int
ssd1306_get_dimension(ssd1306_type_t type, unsigned int* width, unsigned int* height);

int
ssd1306_init(ssd1306_t* handle, const char* device, ssd1306_type_t type);

void
ssd1306_cleanup(ssd1306_t* handle);

const char*
ssd1306_error(ssd1306_t* handle);

int
ssd1306_begin(ssd1306_t* handle, uint8_t vccstate);

int
ssd1306_end(ssd1306_t* handle);

int
ssd1306_display(ssd1306_t* handle, void* image, ssd1306_get_pixel_fn get_pixel);

#endif
