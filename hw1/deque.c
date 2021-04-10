#include <stdlib.h>

#define INITIAL_CAPACITY 64

struct deque {
    char **arr;
    int front;
    int rear;
    int size;
    int capacity;
};

void initDeque(struct deque *d) {
    d->capacity = INITIAL_CAPACITY;
    d->arr = (char **) malloc(sizeof(char *) * d->capacity);
    d->front = d->rear = -1;
    d->size = 0;
}

void addFront(struct deque *d, char *item) {
    if (d->front == 0 && d->rear == d->capacity - 1) { // full, increase capacity
        d->capacity *= 2;
        d->arr = (char **) realloc(d->arr, sizeof(char *) * (d->capacity));
    }

    (d->size)++;
    if (d->front == -1) {
        d->front = d->rear = 0;
        d->arr[d->front] = item;
        return;
    }

    if (d->rear != d->capacity - 1) {
        int j = d->rear + 1;
        for (int i = 1; i <= d->size - 1; i++) {
            d->arr[j] = d->arr[j - 1];
            j--;
        }
        d->arr[j] = item;
        d->front = j;
        (d->rear)++;
    } else {
        (d->front)--;
        d->arr[d->front] = item;
    }
}

char *delFront(struct deque *d) {
    if (d->front == -1)  // empty
        return 0;

    char *item;
    (d->size)--;
    item = d->arr[d->front];
    if (d->front == d->rear)
        d->front = d->rear = -1;
    else
        (d->front)++;
    return item;
}

char *delRear(struct deque *d) {
    if (d->front == -1)  // empty
        return 0;

    char *item;
    (d->size)--;
    item = d->arr[d->rear];
    (d->rear)--;
    if (d->rear == -1)
        d->front = -1;
    return item;
}

void freeDeque(struct deque *d) {
    free(d->arr);
}