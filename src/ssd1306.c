#include "ssd1306.h"
#include <memory.h>
#include <stdlib.h>

#define COUNT_OF(X) (sizeof(X) / sizeof(X[0]))

#define SSD1306_I2C_ADDRESS 0x3C
#define SSD1306_COMMAND(CMD) \
	{ .addr = SSD1306_I2C_ADDRESS, .len = 2, .buf = { 0x00, CMD } }

#define SSD1306_I2C_ADDRESS 0x3C
#define SSD1306_SETCONTRAST 0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_DISPLAYALLON 0xA5
#define SSD1306_NORMALDISPLAY 0xA6
#define SSD1306_INVERTDISPLAY 0xA7
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETCOMPINS 0xDA
#define SSD1306_SETVCOMDETECT 0xDB
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE 0xD9
#define SSD1306_SETMULTIPLEX 0xA8
#define SSD1306_SETLOWCOLUMN 0x00
#define SSD1306_SETHIGHCOLUMN 0x10
#define SSD1306_SETSTARTLINE 0x40
#define SSD1306_MEMORYMODE 0x20
#define SSD1306_COLUMNADDR 0x21
#define SSD1306_PAGEADDR 0x22
#define SSD1306_COMSCANINC 0xC0
#define SSD1306_COMSCANDEC 0xC8
#define SSD1306_SEGREMAP 0xA0
#define SSD1306_CHARGEPUMP 0x8D

#define SSD1306_TRANSFER_BLK_SIZE 16

enum ssd1306_error_e
{
	SSD1306_ERR_I2C = -1,
	SSD1306_ERR_ARG = -2
};

int
ssd1306_get_dimension(ssd1306_type_t type, uint8_t* width, uint8_t* height)
{
	switch(type)
	{
		case SSD1306_128_64:
			*width = 128;
			*height = 64;
			return 0;
		default:
			return SSD1306_ERR_ARG;
	}
}

int
ssd1306_get_buf_size(ssd1306_type_t type, size_t* size)
{
	int error;
	uint8_t width, height;
	if((error = ssd1306_get_dimension(type, &width, &height)) < 0)
	{
		return error;
	}

	*size = width * height;
	return 0;
}

int
ssd1306_init(ssd1306_t* handle, const char* device, ssd1306_type_t type)
{
	memset(handle, 0, sizeof(*handle));

	if(i2c_open(&handle->i2c, device) < 0)
	{
		return handle->error = SSD1306_ERR_I2C;
	}

	handle->type = type;
	return 0;
}

void
ssd1306_cleanup(ssd1306_t* handle)
{
	i2c_close(&handle->i2c);
}

const char*
ssd1306_error(ssd1306_t* handle)
{
	switch(handle->error)
	{
		case SSD1306_ERR_I2C:
			return i2c_errmsg(&handle->i2c);
		case SSD1306_ERR_ARG:
			return "Invalid argument";
		default:
			return NULL;
	}
}

static int
SSD1306_send_commands(ssd1306_t* handle, uint8_t* commands, size_t num_commands)
{
	for(size_t i = 0; i < num_commands; ++i)
	{
		uint8_t buf[] = { 0x00, commands[i] };
		struct i2c_msg msg = {
			.addr = SSD1306_I2C_ADDRESS,
			.len = COUNT_OF(buf),
			.buf = buf
		};

		if(i2c_transfer(&handle->i2c, &msg, 1) < 0)
		{
			return handle->error = SSD1306_ERR_I2C;
		}
	}

	return 0;
}

static int
ssd1306_128_64_begin(ssd1306_t* handle)
{
	uint8_t commands[] = {
		SSD1306_DISPLAYOFF,
		SSD1306_SETDISPLAYCLOCKDIV, 0x80,
		SSD1306_SETMULTIPLEX, 0x3F,
		SSD1306_SETDISPLAYOFFSET, 0x0,
		SSD1306_SETSTARTLINE | 0x0,
		SSD1306_CHARGEPUMP,
			handle->vccstate == SSD1306_EXTERNALVCC ? 0x10 : 0x14,
		SSD1306_MEMORYMODE, 0x00,
		SSD1306_SEGREMAP | 0x1,
		SSD1306_COMSCANDEC,
		SSD1306_SETCOMPINS, 0x12,
		SSD1306_SETCONTRAST,
			handle->vccstate == SSD1306_EXTERNALVCC ? 0x9F : 0xCF,
		SSD1306_SETPRECHARGE,
			handle->vccstate == SSD1306_EXTERNALVCC ? 0x22 : 0xF1,
		SSD1306_SETVCOMDETECT, 0x40,
		SSD1306_DISPLAYALLON_RESUME,
		SSD1306_NORMALDISPLAY,
		SSD1306_DISPLAYON
	};

	return SSD1306_send_commands(handle, commands, COUNT_OF(commands));
}

int
ssd1306_begin(ssd1306_t* handle, uint8_t vccstate)
{
	handle->vccstate = vccstate;
	switch(handle->type)
	{
		case SSD1306_128_64:
			return ssd1306_128_64_begin(handle);
		default:
			return handle->error = SSD1306_ERR_ARG;
	}
}

int
ssd1306_display(ssd1306_t* handle, uint8_t* buffer)
{
	uint8_t width, height;
	int error;
	if((error = ssd1306_get_dimension(handle->type, &width, &height)) < 0)
	{
		return handle->error = error;
	}

	uint8_t commands[] = {
		SSD1306_COLUMNADDR, 0, width - 1,
		SSD1306_PAGEADDR, 0, height / 8 - 1,
	};

	if(SSD1306_send_commands(handle, commands, COUNT_OF(commands) < 0))
	{
		return handle->error = SSD1306_ERR_I2C;
	}

	for(size_t i = 0; i < (width * height / 8); i += SSD1306_TRANSFER_BLK_SIZE)
	{
		uint8_t buf[17];
		buf[0] = 0x40;
		memcpy(&buf[1], &buffer[i], SSD1306_TRANSFER_BLK_SIZE);
		struct i2c_msg msg = {
			.addr = SSD1306_I2C_ADDRESS,
			.len = COUNT_OF(buf),
			.buf = buf
		};

		if(i2c_transfer(&handle->i2c, &msg, 1) < 0)
		{
			return handle->error = SSD1306_ERR_I2C;
		}
	}

	return 0;
}
