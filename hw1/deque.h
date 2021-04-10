#ifndef DEQUE_H
#define DEQUE_H

struct deque {
  char **arr;
  int front;
  int rear;
  int size;
};

void initDeque(struct deque *d);
void addFront(struct deque *d, char *item);
char *delFront(struct deque *d);
char *delRear(struct deque *d);
void freeDeque(struct deque *d);

#endif
