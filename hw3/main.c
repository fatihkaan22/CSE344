#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// TODO: remove before submit
#define BLU "\x1B[36m"
#define RST "\x1B[0m"

#define N 64

sem_t *sem;

struct potato_switch {
  int id; // same as pid of its creator process
  int noswitchesleft;
};

struct shared_memory {
  sem_t all_fifos_created;
  int no_created_fifos;
  int no_potatos;
  struct potato_switch potato_arr[N];
};

void log_send(int potato_id, char *fifoname, int switchno) {
  printf("pid=%d sending potato number %d to %s", getpid(), potato_id,
         fifoname);
  printf("; this is switch number %d\n", switchno);
}

void log_receive(int potato_id, char *fifoname) {
  printf("pid=%d receiving potato number %d from %s\n", getpid(), potato_id,
         fifoname);
}

void log_cooldown(int potato_id) {
  printf("pid=%d; potato number %d has cooled down\n", getpid(), potato_id);
}

void all_fifos_created(void *addr) {
  struct shared_memory *m = addr;
  for (int i = 0; i < m->no_created_fifos; ++i) {
    sem_post(&m->all_fifos_created);
  }
}

// returns decremented switch count
int decrement_switch(int potato_id, const void *addr) {
  int no_potatos;
  memcpy(&no_potatos, addr, sizeof(int));
  struct potato_switch *iter =
      (struct potato_switch *)((int *)addr) + sizeof(int);
  struct potato_switch potato;
  for (int i = 0; i < no_potatos; ++i) {
    potato = *iter;
    if (potato.id == potato_id) {
      potato.noswitchesleft--;
      memcpy(iter, &potato, sizeof(struct potato_switch));
#ifdef DEBUG
      printf(BLU "potato id: %d decremented to %d\n" RST, potato_id,
             potato.noswitchesleft);
#endif
      return potato.noswitchesleft;
    }
    ++iter;
  }
  fprintf(stderr, "error on decrement_switch: potato_id not found\n");
  return -1;
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

int create_fifo(char *filename, char *fifopath, void *addr) {
  struct shared_memory *m = addr;
  FILE *fp = fopen(filename, "r"); // TODO: write, check error
  if (fp == NULL) {
    perror("fopen");
    abort(); // TODO: ???
  }
  /* printf("no fifos: %d\n", get_no_created_fifos(addr)); */
  char buf[512]; // FIXME
  int res_mkfifo;
  char *res_fgets;
  char c;
#ifdef DEBUG
  printf("fifo_no: %d\n", m->no_created_fifos);
#endif
  for (int i = 0; i < m->no_created_fifos;) {
    c = getc(fp);
    if (c == EOF) {
      fprintf(stderr, "out of fifos\n");
      exit(EXIT_FAILURE);
    }
    if (c == '\n')
      ++i;
  }

  res_fgets = fgets(buf, 512, fp);
  buf[strcspn(buf, "\n")] = 0; // remove trailing \n
  res_mkfifo = mkfifo(buf, S_IRUSR | S_IWUSR | S_IWGRP);
  if (res_mkfifo == -1) {
    perror("mkfifo");
    exit(EXIT_FAILURE);
  }
  (m->no_created_fifos)++;

  if (getc(fp) == EOF) {
    all_fifos_created(addr);
  }

  strcpy(fifopath, buf);
  fclose(fp);
  return 0;
}

// returns 0 if shared memory found, 1 if created
int init_shared_mem(void **addr, const char *sharedmem) {
  int fd_mem;
  int shm_size = 512; // FIXME
  bool create_shm = 1;
  // open shared memory (create if not exist)
  fd_mem = shm_open(sharedmem, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd_mem == -1 && errno == EEXIST) {
#ifdef DEBUG
    puts(BLU "shm found" RST); // FIXME
#endif
    create_shm = 0; // TODO: test here w/ new shared memory name
    fd_mem = shm_open(sharedmem, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  }
  if (fd_mem == -1) {
    perror("shm_open");
    exit(EXIT_FAILURE);
  }
  // TODO: consider using semaphores
  if (create_shm == 1 && ftruncate(fd_mem, shm_size) == -1) {
    perror("ftruncate");
    exit(EXIT_FAILURE);
  }
  *addr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_mem, 0);
  if (close(fd_mem) == -1) {
    perror("close");
    exit(EXIT_FAILURE);
  }
  return create_shm;
}

int register_potato(void *addr, int noswitchesleft) {
  struct shared_memory *m = addr;
  m->potato_arr[m->no_potatos].id = getpid();
  m->potato_arr[m->no_potatos].id = noswitchesleft;
  (m->no_potatos)++;
  return 0;
}

int random_fifo(char *fifonames, void *addr, char *fifo_to_write,
                char *exclude_fifo) {
  printf("exclude_fifo: %s\n", exclude_fifo);
  struct shared_memory *m = addr;
  FILE *fp;
  fp = fopen(fifonames, "r");
  char c;
  srand(time(NULL));
  int random = rand();

  if (m->no_created_fifos <= 1) {
    fprintf(stderr, "no fifo open at the moment\n");
		exit(EXIT_FAILURE);
  }

  int line = random % m->no_created_fifos;
  printf("line: %d\n", line);
  for (int i = 0; i < line;) {
    c = getc(fp);
    if (c == '\n')
      ++i;
  }
  if (fgets(fifo_to_write, 512, fp) == NULL) { // FIXME
    fprintf(stderr, "fgets error\n");
  }
  fifo_to_write[strcspn(fifo_to_write, "\n")] = 0;
  if (strcmp(exclude_fifo, fifo_to_write) == 0) {
    // select the next one
    if (line == m->no_created_fifos - 1) {
      fseek(fp, 0, SEEK_SET);
      if (fgets(fifo_to_write, 512, fp) == NULL) { // FIXME
        fprintf(stderr, "fgets error\n");
      }
    } else {
      if (fgets(fifo_to_write, 512, fp) == NULL) { // FIXME
        fprintf(stderr, "fgets error\n");
      }
    }
  }
  fifo_to_write[strcspn(fifo_to_write, "\n")] = 0;
  return 0;
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

  /* sem_init(&sem, 1, 1); */
  sem = sem_open(semaphore, O_CREAT, S_IRUSR | S_IWUSR, 1); // TODO: nolines
  if (sem == SEM_FAILED) {
    perror("sem_open");
    exit(EXIT_FAILURE);
  }
#ifdef DEBUG
  int val = 0;
  sem_getvalue(sem, &val);
  printf("sem: %d\n", val);
#endif

  void *addr;
  int create_shm = init_shared_mem(&addr, sharedmem);

  // if new shared memory created
  if (create_shm == 1) {
#ifdef DEBUG
    printf("%s\n", "new shared memory");
#endif
    struct shared_memory m;
    sem_init(&m.all_fifos_created, 1, 0);
    m.no_created_fifos = 0;
    m.no_potatos = 0;
    memcpy(addr, &m, sizeof(struct shared_memory));
  }

  char fifo_to_read[512]; // FIXME
  create_fifo(fifonames, fifo_to_read, addr);
#ifdef DEBUG
  printf(BLU "%s created\n", fifo_to_read);
  printf("waiting..\n" RST);
#endif
  // wait for all other fifos to be created
  sem_wait(addr);

  printf("%s\n", "done");

  if (noswitches != 0)
    register_potato(addr, noswitches);

#ifdef DEBUG
  printf(BLU);
  puts("== SHARED MEMORY == ");
  struct shared_memory *m = addr;
	int sval;
  sem_getvalue(&m->all_fifos_created, &sval);
  printf("semaphore: %d\n", sval);
  printf("no_created: %d\n", m->no_created_fifos);
  printf("no potato: %d\n", m->no_potatos);
  for (int i = 0; i < m->no_potatos; ++i) {
    printf(BLU "potato: # %d - %d\n" RST, m->potato_arr[i].id,
           m->potato_arr[i].noswitchesleft);
  }
  printf(RST);
#endif

  char fifo_to_write[512];
  random_fifo(fifonames, addr, fifo_to_write, fifo_to_read);

#ifdef DEBUG // sanity check
  if (strcmp(fifo_to_write, fifo_to_read) == 0) {
    printf("random_fifo doesn't work\n");
  }
#endif

  /* if (write() == -1) { */
  /* 	perror("write"); */
  /* 	exit(EXIT_FAILURE); */
  /* } */

#ifdef DEBUG
  printf("fifo_to_write: %s\n", fifo_to_write);
#endif

  int fd_fifow = open(fifo_to_write, O_WRONLY); // FIXME
  if (fd_fifow == -1) {
    fprintf(stderr, "fifo_to_write: %s\n", fifo_to_write);
    perror("open()");
  }
  if (noswitches != 0) { // if have initial potato
    // send potato id
    int potato_id = getpid();
    log_send(potato_id, fifo_to_write, noswitches);
    write(fd_fifow, &potato_id, sizeof(int));
  }

  int potato_id;
  int fd_fifor = open(fifo_to_read, O_RDONLY);
  if (fd_fifor == -1) {
    perror("open(fifo_to_read, O_RDONLY)");
  }
  while (true) {
    printf(BLU "while\n" RST);
#ifdef DEBUG
		printf("read() waiting..");
#endif
    read(fd_fifor, &potato_id, sizeof(int)); // FIXME
    int left = decrement_switch(potato_id, addr);
    if (left == 0) { // cool down
      log_cooldown(potato_id);
      break;
    } else {
      log_receive(potato_id, fifo_to_read);
      log_send(potato_id, fifo_to_write, left);
      write(fd_fifow, &potato_id, sizeof(int));
    }
  }

  // TODO: send to fifo

  // TODO: wait for potato

  /* printf("%d\n", *((int *)addr)); */
  /* printf("%d\n", *((int *)addr + sizeof(int))); */

  /* register_fifo(); */
  /* int fd_fifo = open(fifo_to_read, O_RDONLY); */
  /* if (fd_fifo == -1) { */
  /*   // TODO */
  /* } */
  /* shm_unlink(sharedmem); */
}
