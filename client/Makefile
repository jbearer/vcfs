CFLAGS = -g -Og -Wall -Wextra -Werror

FUSEFLAGS = `pkg-config fuse --cflags --libs`

all: client

clean:
	rm -r client

client: client.c
	$(CC) $(CFLAGS) -o $@ $^ $(FUSEFLAGS)
