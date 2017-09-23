#include <stdio.h>
#include <stdlib.h>
#include <c-periphery/gpio.h>

int
main(int argc, const char* argv[])
{
	(void)argc;
	(void)argv;

	gpio_t gpio;
	if(gpio_open(&gpio, 5, GPIO_DIR_IN) < 0)
	{
		fprintf(stderr, "Cannot connect gpio: %s\n", gpio_errmsg(&gpio));
		return EXIT_FAILURE;
	}

	if(gpio_set_edge(&gpio, GPIO_EDGE_RISING) < 0)
	{
		fprintf(stderr, "Cannot configure gpio: %s\n", gpio_errmsg(&gpio));
		return EXIT_FAILURE;
	}

	bool value;
	if(gpio_read(&gpio, &value) < 0)
	{
		fprintf(stderr, "Cannot read gpio: %s\n", gpio_errmsg(&gpio));
		return EXIT_FAILURE;
	}

	printf("Value: %s\n", value ? "true" : "false");

	return EXIT_SUCCESS;
}
