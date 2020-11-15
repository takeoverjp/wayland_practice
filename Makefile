.PHONY: all clean

all: samiple-client

clean:
	rm -f sample-client *~

samiple-client: main.c
	gcc -g -Og -W -Werror -o $@ $^ -lwayland-client
