#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define N 64
#define BUF_LN 512
#define SHM_SIZE 1024
#define ALL_COOLED_DOWN_MSG -2
#define USR_INTERRUPT_MSG -3

struct potato {
  int id; // same as pid of its creator process
  int switchcount;
  int noswitches;
};

struct shared_memory {
  sem_t all_fifos_created;
  int no_created_fifos;
  int no_potatos;
  struct potato potato_arr[N];
};

struct file {
  char name[256];
  int fd;
};

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

bool usr_interrupt = false;
void handler(int sig) {
  if (sig == SIGINT)
    usr_interrupt = true;
}

void log_send(const struct potato *p, char *fifoname) {
  printf("pid=%d sending potato number %d to %s", getpid(), p->id, fifoname);
  printf("; this is switch number %d / %d\n", p->switchcount, p->noswitches);
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
    if (sem_post(&m->all_fifos_created) == -1) {
      perror("sem_post");
      exit(EXIT_FAILURE);
    }
  }
}

int create_fifo(char *filename, char *fifopath, void *addr) {
  struct shared_memory *m = addr;
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }
  char buf[BUF_LN];
  int res_mkfifo;
  char c;
  for (int i = 0; i < m->no_created_fifos;) {
    c = getc(fp);
    if (c == EOF) {
      fprintf(stderr, "out of fifos\n");
      exit(EXIT_FAILURE);
    }
    if (c == '\n')
      ++i;
  }

  if (fgets(buf, BUF_LN, fp) == NULL) {
    fprintf(stderr, "fgets");
  }
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
  int shm_size = SHM_SIZE;
  bool create_shm = 1;
  // open shared memory (create if not exist)
  fd_mem = shm_open(sharedmem, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd_mem == -1 && errno == EEXIST) {
#ifdef DEBUG
    puts("shm found");
#endif
    create_shm = 0;
    fd_mem = shm_open(sharedmem, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  }
  if (fd_mem == -1) {
    perror("shm_open");
    exit(EXIT_FAILURE);
  }
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

// returns the potato with specified id
struct potato *get_potato(struct potato *arr, int size, int id) {
  struct potato *p;
  for (int i = 0; i < size; ++i) {
    if (arr[i].id == id) {
      p = &arr[i];
      return p;
    }
  }
  fprintf(stderr, "potato not found, id: %d, no_potatos: %d\n", id, size);
  exit(EXIT_FAILURE);
}

// registers potato in shared memory
int register_potato(void *addr, int noswitches) {
  struct shared_memory *m = addr;
  m->potato_arr[m->no_potatos].id = getpid();
  m->potato_arr[m->no_potatos].switchcount = 0;
  m->potato_arr[m->no_potatos].noswitches = noswitches;
  (m->no_potatos)++;
  return 0;
}

// returns index of file descriptor which is opened as O_RDONLY
int open_fifos(char *filename, void *addr, struct file *fifo_fds,
               char *fifo_to_read) {
  struct shared_memory *m = addr;
  int fd_fifor_idx = -1;
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < m->no_created_fifos; ++i) {
    char buf[BUF_LN];
    if (fgets(buf, BUF_LN, fp) == NULL) {
      fprintf(stderr, "fgets");
      exit(EXIT_FAILURE);
    }
    buf[strcspn(buf, "\n")] = 0; // remove trailing \n
    char *name = strrchr(buf, '/');
    if (name == NULL) {
      name = buf;
    } else {
      name++;
    }
    strcpy(fifo_fds[i].name, name);
    if (strcmp(buf, fifo_to_read) == 0) {
      fifo_fds[i].fd = open(buf, O_RDONLY);
      fd_fifor_idx = i;
    } else {
      fifo_fds[i].fd = open(buf, O_WRONLY);
    }
    if (fifo_fds[i].fd == -1)
      perror("open()");
  }
  fclose(fp);
  return fd_fifor_idx;
}

int write_random_fifo(struct file *fifo_fds, int fifo_fds_size,
                      int exclude_fifo_idx, struct potato *p) {
  int random = rand();
  int fd_i = random % fifo_fds_size;
  if (fd_i == exclude_fifo_idx) {
    fd_i++;
    if (fd_i == fifo_fds_size)
      fd_i = 0;
  }
#ifdef DEBUG // sanity check
  if (fd_i == exclude_fifo_idx) {
    printf("random_fifo doesn't work\n");
  }
#endif
  log_send(p, fifo_fds[fd_i].name);
  write(fifo_fds[fd_i].fd, &p->id, sizeof(int));
  return 0;
}

// send message to fifos: (ALL_COOLED_DOWN_MSG, USR_INTERRUPT_MSG)
int let_others_know(struct file *fifo_fds, int fifo_fds_size,
                    int exclude_fifo_idx, int msg) {
  for (int i = 0; i < fifo_fds_size; ++i) {
    if (i == exclude_fifo_idx)
      continue;
    if (write(fifo_fds[i].fd, &msg, sizeof(int)) == -1) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }
  return 0;
}

bool all_cooled_down(struct potato *arr, int size) {
  for (int i = 0; i < size; ++i) {
    if (arr[i].switchcount != arr[i].noswitches) {
      return false;
    }
  }
  return true;
}

int main(int argc, char **argv) {
  int opt;
  int noswitches = -1;
  char *sharedmem = NULL;
  char *fifonames = NULL;
  char *semaphore = NULL;

  signal(SIGINT, handler);

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
  // check unentered optarg
  if (noswitches == -1 || sharedmem == NULL || fifonames == NULL ||
      semaphore == NULL) {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  srand(time(NULL)); // for random

  sem_t *sem;
  sem = sem_open(semaphore, O_CREAT, S_IRUSR | S_IWUSR, 1);
  if (sem == SEM_FAILED) {
    perror("sem_open");
    exit(EXIT_FAILURE);
  }
#ifdef DEBUG
  int val = 0;
  sem_getvalue(sem, &val);
  printf("semaphore: %d\n", val);
#endif
  // only 1 process is going to try to initialize
  // also try to get fifo, since it involves shared memory i/o
  if (sem_wait(sem) == -1) {
    perror("sem_wait");
    exit(EXIT_FAILURE);
  }
  void *addr;
  int create_shm = init_shared_mem(&addr, sharedmem);

  // if new shared memory created
  if (create_shm == 1) {
#ifdef DEBUG
    printf("%s\n", "new shared memory");
#endif
    struct shared_memory m;
    if (sem_init(&m.all_fifos_created, 1, 0) == -1) {
			perror("sem_init");
			exit(EXIT_FAILURE);
		}
    m.no_created_fifos = 0;
    m.no_potatos = 0;
    memcpy(addr, &m, sizeof(struct shared_memory));
  }

  char fifo_to_read[BUF_LN];
  create_fifo(fifonames, fifo_to_read, addr);
  if (sem_post(sem) == -1) {
    perror("sem_post");
    exit(EXIT_FAILURE);
  }
#ifdef DEBUG
  printf("%s created\n", fifo_to_read);
  printf("waiting for others to create their fifos...\n");
#endif
  // wait for all other fifos to be created
  struct shared_memory *m = addr;

  if (noswitches != 0) {
    if (sem_wait(sem) == -1) {
      perror("sem_wait");
      exit(EXIT_FAILURE);
    }
    // critical section: shared memory i/o
    register_potato(addr, noswitches);
    if (sem_post(sem) == -1) {
      perror("sem_post");
      exit(EXIT_FAILURE);
    }
  }

  if (usr_interrupt) {
#ifdef DEBUG
    puts("User Interrupt");
#endif
    if (shm_unlink(sharedmem) == -1 && errno != ENOENT)
      perror("shm_unlink");
    if (sem_unlink(semaphore) == -1 && errno != ENOENT)
      perror("sem_unlink");
    exit(EXIT_SUCCESS);
  }

  if (sem_wait(&m->all_fifos_created) == -1) {
    perror("sem_wait");
    exit(EXIT_FAILURE);
  }

#ifdef DEBUG
  puts("== SHARED MEMORY == ");
  int sval;
  sem_getvalue(&m->all_fifos_created, &sval);
  printf("semaphore: %d\n", sval);
  printf("no_created: %d\n", m->no_created_fifos);
  printf("no potato: %d\n", m->no_potatos);
  for (int i = 0; i < m->no_potatos; ++i) {
    printf("potato: # %d - %d/%d\n", m->potato_arr[i].id,
           m->potato_arr[i].switchcount, m->potato_arr[i].noswitches);
  }
#endif

  struct file fifo_fds[N];
  int fifor_fd_idx = open_fifos(fifonames, addr, fifo_fds, fifo_to_read);
  int fifor_fd = fifo_fds[fifor_fd_idx].fd;
  char *fifor_name = fifo_fds[fifor_fd_idx].name;

  if (noswitches != 0) { // if have an initial potato
    if (sem_wait(sem) == -1) {
      perror("sem_wait");
      exit(EXIT_FAILURE);
    }
    struct potato *p = get_potato(m->potato_arr, m->no_potatos, getpid());
    p->switchcount++;
    write_random_fifo(fifo_fds, m->no_created_fifos, fifor_fd_idx, p);
    if (sem_post(sem) == -1) {
      perror("sem_post");
      exit(EXIT_FAILURE);
    }
  }

  int msg;
  while (!usr_interrupt) {
    if (read(fifor_fd, &msg, sizeof(int)) == -1)
      perror("read");
    if (msg == ALL_COOLED_DOWN_MSG || msg == USR_INTERRUPT_MSG) {
      break;
    }
    int potato_id = msg;
    if (sem_wait(sem) == -1) {
      perror("sem_wait");
      exit(EXIT_FAILURE);
    }
    struct potato *p = get_potato(m->potato_arr, m->no_potatos, potato_id);
    p->switchcount++;
    if (p->switchcount == p->noswitches) { // cool down
      log_cooldown(potato_id);
      if (all_cooled_down(m->potato_arr, m->no_potatos)) {
        let_others_know(fifo_fds, m->no_created_fifos, fifor_fd_idx,
                        ALL_COOLED_DOWN_MSG);
        break;
      }
    } else {
      log_receive(potato_id, fifor_name);
      write_random_fifo(fifo_fds, m->no_created_fifos, fifor_fd_idx, p);
    }
    if (sem_post(sem) == -1) {
      perror("sem_post");
      exit(EXIT_FAILURE);
    }
  }

  int status = EXIT_SUCCESS;
  if (usr_interrupt) {
#ifdef DEBUG
    puts("User Interrupt");
#endif
    let_others_know(fifo_fds, m->no_created_fifos, fifor_fd_idx,
                    USR_INTERRUPT_MSG);
  }

  // free resources
  for (int i = 0; i < m->no_created_fifos; ++i) {
    if (close(fifo_fds[i].fd) == -1)
      perror("close");
    status = EXIT_FAILURE;
  }

  if (sem_destroy(&m->all_fifos_created) == -1) {
    perror("sem_close");
    status = EXIT_FAILURE;
  }

  if (shm_unlink(sharedmem) == -1 && errno != ENOENT) {
    perror("shm_unlink");
    status = EXIT_FAILURE;
  }
  sem_close(sem);
  if (sem_unlink(semaphore) == -1 && errno != ENOENT) {
    perror("sem_unlink");
    status = EXIT_FAILURE;
  }
  exit(status);
}
