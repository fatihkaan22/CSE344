#ifndef DEQUE_H
#define DEQUE_H

struct queue {
  char *arr;
  int front;
  int rear;
  int CAPACITY;
};

void init_queue(struct queue *q, int capacity);
void offer(struct queue *q, char item);
char poll(struct queue *q);
void free_queue(struct queue *q);

#endif
