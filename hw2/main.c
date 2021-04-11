#include <signal.h>
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

static volatile sig_atomic_t gotSIGUSR1 = 0;
static volatile sig_atomic_t gotSIGUSR2 = 0;
static volatile sig_atomic_t noChildDone = 0;

static void handler(int sig) {
  if (sig == SIGUSR1) {
    perror("SIGUSR1");
    /* printf("%s\n", "SIGUSR1 arrived"); */
    noChildDone++;
    gotSIGUSR1++;
  }
  if (sig == SIGUSR2) {
    gotSIGUSR2++;
    /* printf("%s\n", "SIGUSR2 arrived"); */
  }
}

float lagrange(struct coord *coords, int degree, int interpolateIndex) {
  int n = degree + 1;
  float sum = 0;
  float p;
  for (int i = 0; i < n; i++) {
    p = 1;
    for (int j = 0; j < n; j++) {
      if (j != i)
        p *= (coords[interpolateIndex].x - coords[j].x) /
             (coords[i].x - coords[j].x);
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
    perror("fcntl, lock");
    return -1;
  }

  if (fgets(buf, buf_size, fp) == NULL) {
    fprintf(stderr, "fgets\n");
  }
  // unlock
  fl.l_type = F_UNLCK;
  if (fcntl(fileno(fp), F_UNLCK, &fl) == -1) {
    perror("fcntl, unlock");
  }
  /* puts(buf); */
  return 0;
}

void child(FILE *fp) {
  sigset_t zeromask, originalmask;
  int buf_size = 256;
  char buf[256];
  struct coord coords[COLS];

  sigemptyset(&zeromask);
  sigemptyset(&originalmask);
  sigaddset(&originalmask, SIGUSR1);
  sigprocmask(SIG_SETMASK, &originalmask, NULL);

  // round 1
  readLine(fp, buf, buf_size);
  fillCoords(coords, buf);
  float res = lagrange(coords, 5, 7);
  printf("Interpolate value: %.1f\n", res);

  /* sleep(1); */
  kill(getppid(), SIGUSR1);

  printf("suspending child\n");
  if (sigsuspend(&zeromask) != -1)
    perror("sigsuspend error");
  printf("returned from suspend\n");
  // round 2
  printf("%s\n", "yey");
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Invalid number of arguements\n");
    exit(EXIT_FAILURE);
  }
  char *filename = argv[1];

  sigset_t tempmask;
  struct sigaction act;
  act.sa_handler = handler;
  act.sa_flags = 0;
  sigemptyset(&act.sa_mask);

  sigemptyset(&tempmask);
  sigaddset(&tempmask, SIGUSR2);

  if (sigaction(SIGUSR1, &act, 0) == -1) {
    perror("Unexpected error while attempting to pre-conditions");
  }

  // block SIGUSR
  /* sigset_t mask; */
  /* sigemptyset(&mask); */
  /* sigaddset(&mask, SIGUSR1); */
  /* sigaddset(&mask, SIGUSR2); */
  /* if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) { */
  /*   perror("sigprocmask"); */
  /*   exit(EXIT_FAILURE); */
  /* } */

  /* // SIGUSR handler */
  /* struct sigaction sa; */
  /* sigemptyset(&sa.sa_mask); */
  /* sa.sa_flags = 0; */
  /* sa.sa_handler = handler; */
  /* if (sigaction(SIGUSR1, &sa, NULL) == -1) { */
  /*   perror("sigaction"); */
  /*   exit(EXIT_FAILURE); */
  /* } */
  /* if (sigaction(SIGUSR2, &sa, NULL) == -1) { */
  /*   perror("sigaction"); */
  /*   exit(EXIT_FAILURE); */
  /* } */

  FILE *fp = fopen(filename, "r+"); // TODO: write, check error
  if (fp == NULL) {
    perror("fopen");
    abort();
  }

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

  sigset_t mask;
  sigemptyset(&mask);

  sleep(1);

  while (noChildDone < 8) {
    printf("Parent %d waiting for SIGNAL\n", getpid());
    printf("noChildDone: %d\n", noChildDone);

    sigset_t zeromask, originalmask;
    sigemptyset(&zeromask);
    sigemptyset(&originalmask);
    sigaddset(&originalmask, SIGUSR1);
    sigprocmask(SIG_SETMASK, &originalmask, NULL);
    sigsuspend(&zeromask);
    /* sigprocmask(SIG_UNBLOCK, &oldmask, NULL); */
  }

  for (int i = 0; i < n; ++i) {
    kill(pids[i], SIGUSR1);
  }

  int status;
  pid_t pid;
  for (int i = 0; i < n; ++i) {
    pid = wait(&status);
    if (pid == -1) {
      perror("wait");
    }
    printf("Child with PID %ld exited with status %d.\n", (long)pid, status);
  }

  for (int i = 0; i < ROWS; ++i) {
    printf("%d\n", pids[i]);
  }

  /* fclose(fp); */
  return 0;
}
