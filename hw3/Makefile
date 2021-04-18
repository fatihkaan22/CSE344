CC = gcc
CFLAGS = -pedantic-errors -Wall -std=gnu99 -g -lrt
CFLAGS += -DDEBUG

HDRS := $(wildcard *.h)

SRCS := $(wildcard *.c)
# SRCS := hw1.c

OBJS := $(SRCS:.c=.o)
# OBJS := hw1.o deque.o

EXEC := player

%.o: %.c $(HDRS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXEC) $(OBJS)
	rm -f ./fifos/* # TODO: delete this line before submit
