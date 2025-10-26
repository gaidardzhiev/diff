CC=gcc
FLAGS=-march=armv8-a+simd+crc -O3
BIN=diff
#ARCH=$(shell [ "`uname -m`" != "x86" ] && printf "WARNING: this program is designed specifically for 32bit armv8l and may FAIL to run on `%s` architecture...\n" "`uname -m`" || true)

all: warning $(BIN)

warning:
	@sh -c '[ "`uname -m`" != "armv8l" ] && printf "WARNING: this program is designed specifically for 32bit armv8l and may FAIL to run on `%s` architecture...\n" "`uname -m`" || true'


$(BIN): %: %.c
	$(CC) $(FLAGS) -o $@ $<

install:
	cp $(BIN) /usr/bin/$(BIN)

clean:
	rm -f $(BIN)
