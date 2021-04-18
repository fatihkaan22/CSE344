#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// TODO: remove before submit
#define BLU "\x1B[36m"
#define RST "\x1B[0m"

struct potato_switch {
  int id; // same as pid of its creator process
  int noswitchesleft;
};

char *create_fifo(char *filename, char *fifopath) {
  FILE *fp = fopen(filename, "r"); // TODO: write, check error
  if (fp == NULL) {
    perror("fopen");
    abort(); // TODO: ???
  }

  char buf[512]; // FIXME
  int res_mkfifo;
  char *res_fgets;
  do {
    res_fgets = fgets(buf, 512, fp);
    buf[strcspn(buf, "\n")] = 0; // remove trailing \n
    res_mkfifo = mkfifo(buf, S_IRUSR | S_IWUSR | S_IWGRP);
  } while (res_mkfifo == -1 && errno == EEXIST && res_fgets != NULL);
  if (res_mkfifo == -1) {
    perror("mkfifo");
    exit(EXIT_FAILURE);
  }
  strcpy(fifopath, buf);
  fclose(fp);
}

void usage(char *argv0) {
  printf(
      "Usage: %s -b haspatatoornot -s nameofsharedmemory -f filewithfifonames\n"
      " -b haspatatoornot        how many times the potato has to be switched "
      "between processes in order to cool down\n"
      " -s nameofsharedmemory    the name of the posix named shared memory  "
      "segment  used  for  internal  IPC\n"
      " -f filewithfifonames     ascii file will contain the names of the "
      "fifoes that will be used to communicate among the processes\n"
      " -m namedsemaphore         named posix semaphore to be used for "
      "synchronization\n",
      argv0);
}

int main(int argc, char **argv) {
  int opt;
  int noswitches;
  char *sharedmem;
  char *fifonames;
  char *semaphore;

  while ((opt = getopt(argc, argv, "b:s:f:m:")) != -1) {
    switch (opt) {
    case 'b': {
      char *endptr;
      errno = 0;
      noswitches = strtol(optarg, &endptr, 10);
      if (errno != 0) {
        perror("strtol");
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
      if (endptr == optarg) {
        fprintf(stderr, "No digits were found\n");
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
    } break;
    case 's':
      sharedmem = optarg;
      break;
    case 'f':
      fifonames = optarg;
      break;
    case 'm':
      semaphore = optarg;
      break;
    default:
      usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  // TODO: check unentered optarg

  char fifopath[512]; // FIXME
  create_fifo(fifonames, fifopath);
#ifndef DEBUG
  printf("%s created\n", fifopath);
#endif

  int fd_mem;
  int shm_size = 256; // FIXME
  void *addr;
  bool create_shm = true;

  // open shared memory (create if not exist)
  fd_mem = shm_open(sharedmem, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd_mem == -1 && errno == EEXIST) {
#ifndef DEBUG
    printf("%s\n", "yey: shm found"); // FIXME
#endif
    create_shm = false; // TODO: test here w/ new shared memory name
    fd_mem = shm_open(sharedmem, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  }
  if (fd_mem == -1) {
    perror("shm_open");
    exit(EXIT_FAILURE);
  }

  // TODO: consider using semaphores
  if (create_shm && ftruncate(fd_mem, shm_size) == -1) {
    perror("ftruncate");
    exit(EXIT_FAILURE);
  }

  addr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_mem, 0);
  if (close(fd_mem) == -1) {
    perror("close");
    exit(EXIT_FAILURE);
  }

  // get number of items in shared memory
  int no_items = 0;
  if (create_shm) {
    printf("%s\n", "new shared memory");
    memcpy(addr, &no_items, sizeof(int));
  } else {
    memcpy(&no_items, addr, sizeof(int));
    (*(int *)addr)++; // increment size
  }

  // iterate first free position
  struct potato_switch *iter =
      (struct potato_switch *)((int *)addr) + sizeof(int);
  for (int i = 0; i < no_items; ++i)
    ++iter;

  struct potato_switch potato;
  potato.id = getpid();
  potato.noswitchesleft = noswitches;

  // register patato
  memcpy(iter, &potato, sizeof(struct potato_switch));

#ifdef DEBUG
  printf(BLU);
  printf("no items: %d\n", no_items);
  iter = (struct potato_switch *)((int *)addr) + sizeof(int);
  for (int i = 0; i < no_items; ++i) {
    struct potato_switch potato2 = *iter;
    printf(BLU "potato: %d - %d\n" RST, potato2.id, potato2.noswitchesleft);
    ++iter;
  }
  printf(RST);
#endif

  /* printf("%d\n", *((int *)addr)); */
  /* printf("%d\n", *((int *)addr + sizeof(int))); */

  /* register_fifo(); */
  /* int fd_fifo = open(fifopath, O_RDONLY); */
  /* if (fd_fifo == -1) { */
  /*   // TODO */
  /* } */
  /* shm_unlink(sharedmem); */

}
