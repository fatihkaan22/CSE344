CC = gcc
# CFLAGS = -pedantic-errors -Wall -std=gnu99 -g -lrt -lpthread
CFLAGS = -pedantic-errors -Wall -std=gnu99 -g -lpthread
# CFLAGS += -DDEBUG

SRCS := main.c queue.c queue.h

OBJS := $(SRCS:.c=.o)

EXEC := program

%.o: %.c $(HDRS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXEC) $(OBJS)
