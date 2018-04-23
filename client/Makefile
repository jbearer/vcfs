CFLAGS = -g -Og -Wall -Wextra -Werror

FUSEFLAGS = `pkg-config fuse --cflags --libs`

all: vcfs-client

clean:
	rm -r vcfs-client

vcfs-client: client.c
	$(CC) $(CFLAGS) -o $@ $^ $(FUSEFLAGS)
