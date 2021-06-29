CC = gcc
CFLAGS = -pedantic-errors -Wall -std=gnu99 -g -lpthread
# CFLAGS += -DDEBUG

SRCS_S := server.c csv_reader.c query.c queue.c socket_io.c
SRCS_C := client.c socket_io.c

OBJS_S := $(SRCS_S:.c=.o)
OBJS_C := $(SRCS_C:.c=.o)

EXEC_S := server
EXEC_C := client

%.o: %.c $(HDRS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(EXEC_S) $(EXEC_C)

$(EXEC_S): $(OBJS_S)
	$(CC) -o $@ $^ $(CFLAGS)

$(EXEC_C): $(OBJS_C)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXEC_S) $(OBJS_S)
	rm -f $(EXEC_C) $(OBJS_C)
