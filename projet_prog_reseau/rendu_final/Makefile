CC = cc
CFLAGS = -Wall -Wextra

.PHONY: all clean

all: client server

client: client.c common.h include/client.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

server: server.c common.h include/server.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) client server
