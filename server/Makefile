CFLAGS = -g -Wall -Wextra -Werror

all: server

clean:
	rm -r server

server: server.c
	$(CC) $(CFLAGS) -o $@ $^
