all: hakomari-displayd

hakomari-displayd: hakomari-displayd.c ssd1306.c ssd1306_gd.c
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery -lgd
