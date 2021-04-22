CC = gcc
CFLAGS = -pedantic-errors -Wall -std=gnu99 -g -lrt -lpthread
CFLAGS += -DDEBUG

HDRS := $(wildcard *.h)

SRCS := main.c

OBJS := $(SRCS:.c=.o)

EXEC := player

%.o: %.c $(HDRS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXEC) $(OBJS)
	rm -f ./fifos/* # TODO: delete this line before submit
