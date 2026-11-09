CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
CPPFLAGS = -Icore -Ilinux
LDLIBS =

.PHONY: all test clean
all: bin/pme-cli bin/pme-gui

bin:
	mkdir -p bin

bin/pme-cli: core/pme.c core/pme.h linux/serial.c linux/serial.h linux/cli.c | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) core/pme.c linux/serial.c linux/cli.c -o $@

bin/pme-gui: core/pme.c core/pme.h linux/serial.c linux/serial.h linux/gui.c | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) core/pme.c linux/serial.c linux/gui.c -lX11 -lpthread -o $@

bin/core-test: core/pme.c core/pme.h tests/core_test.c | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) core/pme.c tests/core_test.c -o $@

test: bin/core-test
	./bin/core-test

clean:
	rm -rf bin
