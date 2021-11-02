CC=gcc
CFLAGS=-O3 -Wall -pedantic
LDFLAGS=-l SDL2 -l SDL2_ttf

hexing:
	$(CC) main.c $(CFLAGS) $(LDFLAGS) -o $@

.PHONY: clean
clean:
	rm -f hexing
