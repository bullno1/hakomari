all: hakomari-displayd hakomari-dispatcherd hakomari-show hakomari-echod hakomari-echo

hakomari-rpc.a: hakomari-rpc.c
	$(CC) $(CFLAGS) -o hakomari-rpc.o -c $<
	$(AR) rcs $@ hakomari-rpc.o

hakomari-show: hakomari-show.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-echod: hakomari-echod.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-echo: hakomari-echo.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-displayd: hakomari-displayd.c ssd1306.c ssd1306_gd.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery -lgd -lcmp

hakomari-dispatcherd: hakomari-dispatcherd.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery -lcmp
