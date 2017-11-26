all: \
	hakomari-dispatcherd \
	hakomari-displayd hakomari-show \
	hakomari-echod hakomari-echo \
	hakomari-endpointd \
	hakomari-vaultd \
	hakomari-ask-passphrase \
	hakomari-remember-passphrase \
	hakomari-workerd \
	hakomari-worker-exec \

ssd1306.a: ssd1306.c
	$(CC) $(CFLAGS) -o ssd1306.o -c $<
	$(AR) rcs $@ ssd1306.o

ssd1306_gd.a: ssd1306_gd.c
	$(CC) $(CFLAGS) -o ssd1306_gd.o -c $<
	$(AR) rcs $@ ssd1306_gd.o

hakomari-rpc.a: hakomari-rpc.c
	$(CC) $(CFLAGS) -o hakomari-rpc.o -c $<
	$(AR) rcs $@ hakomari-rpc.o

hakomari-show: hakomari-show.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-echod: hakomari-echod.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-echo: hakomari-echo.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-endpointd: hakomari-endpointd.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp -lancillary

hakomari-displayd: hakomari-displayd.c ssd1306.a ssd1306_gd.a hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery -lgd -lcmp -lancillary

hakomari-dispatcherd: hakomari-dispatcherd.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery -lcmp -lancillary

hakomari-vaultd: hakomari-vaultd.c ssd1306_gd.a ssd1306.a hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lc-periphery -lgd -lcmp -lancillary

hakomari-ask-passphrase: hakomari-ask-passphrase.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-remember-passphrase: hakomari-remember-passphrase.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-workerd: hakomari-workerd.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp

hakomari-worker-exec: hakomari-worker-exec.c hakomari-rpc.a
	$(CC) $(CFLAGS) -o $@ $^ -lcmp
