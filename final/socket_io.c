#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "socket_io.h"

int send_int(int num, int fd) {
  int32_t conv = htonl(num);
  char *data = (char *)&conv;
  int left = sizeof(conv);
  int n;
  do {
    n = write(fd, data, left);
    if (n < 0) {
      perror("write()");
      return -1;
    } else {
      data += n;
      left -= n;
    }
  } while (left > 0);
  return 0;
}

int receive_int(int *num, int fd) {
  int32_t ret;
  char *data = (char *)&ret;
  int left = sizeof(ret);
  int n;
  do {
    n = read(fd, data, left);
    if (n <= 0) {
      perror("read()");
      return -1;
    } else {
      data += n;
      left -= n;
    }
  } while (left > 0);
  *num = ntohl(ret);
  return 0;
}

int send_line(char *buffer, int len, int fd) {
  send_int(len, fd);
  int left = len;
  int n;
  do {
    n = write(fd, buffer, left);
    if (n < 0) {
      perror("write()");
      return -1;
    } else if (n == 0) {
      return 1;
    } else {
      buffer += n;
      left -= n;
    }
  } while (left > 0);
  return 0;
}

int receive_line(char *buffer, int fd) {
  int left;
  receive_int(&left, fd);
  int n;

  do {
    n = read(fd, buffer, left);
    if (n < 0) {
      perror("read()");
      return -1;
    } else if (n == 0) {
      return 1;
    } else {
      buffer += n;
      left -= n;
    }
  } while (left > 0);
  return 0;
}

// in seconds
void print_time_diff(FILE *f, struct timespec t1, struct timespec t2) {
  struct timespec delta;
  unsigned int NS_PER_SECOND = 1000000000;
  delta.tv_nsec = t2.tv_nsec - t1.tv_nsec;
  delta.tv_sec = t2.tv_sec - t1.tv_sec;
  if (delta.tv_sec > 0 && delta.tv_nsec < 0) {
    delta.tv_nsec += NS_PER_SECOND;
    delta.tv_sec--;
  } else if (delta.tv_sec < 0 && delta.tv_nsec > 0) {
    delta.tv_nsec -= NS_PER_SECOND;
    delta.tv_sec++;
  }
  fprintf(f,"%d.%.6ld", (int)delta.tv_sec, delta.tv_nsec / 1000);
}

void print_timestamp(FILE *f) {
  time_t timer;
  char buffer[32];
  struct tm *tm_info;
  timer = time(NULL);
  tm_info = localtime(&timer);
  strftime(buffer, 26, "%F %T", tm_info);
  fprintf(f, "[%s] ", buffer);
}
