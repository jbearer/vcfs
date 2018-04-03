CFLAGS = -g -Wall -Wextra -Werror

all: server hook

clean:
	rm -r server

server: server.c
	$(CC) $(CFLAGS) -o $@ $^

hook: hook.c
	$(CC) $(CFLAGS) -o $@ $^
