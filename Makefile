CC= gcc
FLAGS=-march=armv8-a+simd+crc -O3
BIN=diff

all: $(BIN)

$(BIN): %: %.c
	$(CC) $(FLAGS) -o $@ $<

install:
	cp $(BIN) /usr/bin/$(BIN)

clean:
	rm -f $(BIN)
