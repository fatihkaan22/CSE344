#ifndef DEQUE_H
#define DEQUE_H

struct queue {
  int *arr;
  int front;
  int rear;
  int size;
  int CAPACITY;
};

void init_queue(struct queue *q, int capacity);
void offer(struct queue *q, int item);
int poll(struct queue *q);
void free_queue(struct queue *q);

#endif
