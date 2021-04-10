#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROWS 8
#define COLS 8

struct coord {
  float x;
  float y;
};

float lagrange(struct coord *coords, int degree, int interpolateIndex) {
  int n = degree + 1;
  float sum = 0;
  float p;
  for (int i = 0; i < n; i++) {
    p = 1;
    for (int j = 0; j < n; j++) {
      if (j != i)
        p *= (coords[interpolateIndex].x - coords[j].x) / (coords[i].x - coords[j].x);
    }
    p *= coords[i].y;
    sum += p;
  }
  return sum;
}

int fillCoords(struct coord *coords, char *buf) {
  char *rest = buf;

  for (int i = 0; i < COLS * 2; ++i) {
    coords[i].x = strtod(rest, &rest);
    // TODO: consider checking white spaces
    rest++;
    coords[i].y = strtod(rest, &rest);
    rest++;
  }

  return 0;
}

int readLine(FILE *fp, char *buf, int buf_size) {
  struct flock fl;

  // lock
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  fl.l_pid = getpid();

  if (fcntl(fileno(fp), F_SETLKW, &fl) == -1) {
    perror("fcntl");
    return -1; // TODO: return -1
  }

  /* FILE *fp = fdopen(fd, "r+"); */
  /* if (fp == NULL) { */
  /* perror("fdopen"); */
  /* } */

  if (fgets(buf, buf_size, fp) == NULL) {
    fprintf(stderr, "fgets\n");
  }
  // unlock
  fl.l_type = F_UNLCK;
  fcntl(fileno(fp), F_UNLCK, &fl);
  /* puts(buf); */
  return 0;
}

void child(FILE *fp) {
  int buf_size = 256;
  char buf[256];
  struct coord coords[COLS];
  readLine(fp, buf, buf_size);
  fillCoords(coords, buf);
  /* for (int i = 0; i < COLS; ++i) { */
  /*   printf("(%.1f,%.1f)", coords[i].x, coords[i].y); */
  /* } */
  /* puts(""); */
  float res = lagrange(coords, 5, 7);
  printf("Interpolate value: %.1f\n", res);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Invalid number of arguements\n");
    exit(EXIT_FAILURE);
  }
  char *filename = argv[1];

  FILE *fp = fopen(filename, "r+"); // TODO: write, check error
  /* FILE *fp = fopen(filename, "r"); */

  pid_t pids[ROWS];
  int n = ROWS;

  for (int i = 0; i < n; ++i) {
    switch (pids[i] = fork()) {
    case -1:
      perror("fork");
      abort();
    case 0:
      child(fp);
      exit(EXIT_SUCCESS);
    default:
      /* wait(&s); */
      break;
    }
  }

  int status;
  pid_t pid;
  while (n > 0) {
    pid = wait(&status);
    printf("Child with PID %ld exited with status 0x%x.\n", (long)pid, status);
    --n; // TODO Remove pid from the pids array.
  }

  for (int i = 0; i < ROWS; ++i) {
    printf("%d\n", pids[i]);
  }

  /* fclose(fp); */
  return 0;
}
