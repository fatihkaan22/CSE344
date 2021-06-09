#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

void init_queue(struct queue *q, int capacity) {
  q->CAPACITY = capacity;
  q->arr = malloc(sizeof(int) * q->CAPACITY);
  q->front = q->rear = -1;
  q->size = 0;
}

void offer(struct queue *q, int item) {
  if ((q->front == q->rear + 1) ||
      (q->front == 0 && q->rear == q->CAPACITY - 1)) // full
    printf("\n Queue is full!! \n");
  else {
    q->size++;
    if (q->front == -1)
      q->front = 0;
    q->rear = (q->rear + 1) % q->CAPACITY;
    q->arr[q->rear] = item;
  }
}

int poll(struct queue *q) {
  if (q->front == -1) { // empty
    printf("\n Queue is empty !! \n");
    return -1;
  } else {
    q->size--;
    char item = q->arr[q->front];
    if (q->front == q->rear) {
      q->front = q->rear = -1;
    }
    else {
      q->front = (q->front + 1) % q->CAPACITY;
    }
    return item;
  }
}

void free_queue(struct queue *q) { free(q->arr); }
