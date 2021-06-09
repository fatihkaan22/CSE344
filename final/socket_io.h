#ifndef SOCKET_IO_H
#define SOCKET_IO_H

#include <stdio.h>

int send_int(int num, int fd);
int receive_int(int *num, int fd);
int send_line(char *buffer, int len, int fd);
int receive_line(char *buffer, int fd);
void print_time_diff(struct timespec t1, struct timespec t2);
void print_timestamp(FILE *f);

#endif /* end of include guard: SOCKET_IO_H */
