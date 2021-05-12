#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

void init_queue(struct queue *d, int capacity) {
	d->CAPACITY = capacity;
	d->arr = malloc(sizeof(char) * d->CAPACITY);
  d->front = d->rear = -1;
}

void offer(struct queue *d, char item) {
  if (d->rear == d->CAPACITY - 1)
    printf("\nQueue is Full!!");
  else {
    if (d->front == -1)
      d->front = 0;
    d->rear++;
    d->arr[d->rear] = item;
  }
}

char poll(struct queue *d) {
	char c = -1;
  if (d->front == -1)
    printf("\nQueue is Empty!!");
  else {
    c = d->arr[d->front];
    d->front++;
    if (d->front > d->rear)
      d->front = d->rear = -1;
  }
	return c;
}

void free_queue(struct queue *d) { free(d->arr); }
