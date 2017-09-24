all: hakomari-displayd

hakomari-displayd: hakomari-displayd.c ssd1306.c
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery
