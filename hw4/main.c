#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define S_32 32 // TODO: make dynamic

#define R "\x1B[31m"
#define C "\x1B[36m"
#define G "\x1B[32m"
#define Y "\x1B[33m"
#define T "\x1B[0m"

#define EMPTY 'e'

// shared memories
struct shared_memory *mem;
int memsize = 0;
int *citizen_pids;

struct shared_memory {
  sem_t sem, empty, vaccines_available;
  int novacc1;
  int novacc2;
  int bufsize;
  int next_citizen_idx;
  int current_tour;
};

void handler(int signum) {
  switch (signum) {
  case SIGUSR1:
    /* puts("SIGUSR1"); */
    break;
  default:
    fprintf(stderr, "handler caught unexpected signal %d\n", signum);
  }
}

void log_nurse(int id, int pid, char vaccine, int total1, int total2) {
  printf("Nurse %d (pid=%d) has brought vaccine %c: the clinic has %d vaccine1 "
         "and %d vaccine2.\n",
         id, pid, vaccine, total1, total2);
  fflush(stdout);
}

void log_welcome(int c, int t) {
  printf("Welcome to GTU344 clinic. Number of citizens to vaccinate "
         "c=%d with t=%d doeses.\n",
         c, t);
}

int add_vaccine(char c) {
  // TODO: use buffer
  switch (c) {
  case '1':
    mem->novacc1++;
    break;
  case '2':
    mem->novacc2++;
    break;
  default:
    fprintf(stderr, "Unexpected character: %c\n", c);
  }
  return 0;
}

int remove_vaccines() {
  // TODO: use buffer
  mem->novacc1--;
  mem->novacc2--;
  return 0;
}

int invite_citizen(int pid) {
  printf("kill pid: %d\n", pid);
  kill(pid, SIGUSR1);
  return 0;
}

void init_mem(int bufsize) {
  mem->bufsize = bufsize;
  mem->novacc1 = mem->novacc2 = 0;
  mem->next_citizen_idx = 0;
  mem->current_tour = 0;
  if (sem_init(&mem->sem, 1, 1) == -1) {
    perror("sem_init");
    exit(EXIT_FAILURE);
  }
  printf("%d\n", bufsize);
  if (sem_init(&mem->empty, 1, bufsize) == -1) {
    perror("sem_init");
    exit(EXIT_FAILURE);
  }
  /* if (sem_init(&mem->full, 1, 0) == -1) { */
  /*   perror("sem_init"); */
  /*   exit(EXIT_FAILURE); */
  /* } */
  if (sem_init(&mem->vaccines_available, 1, 0) == -1) {
    perror("sem_init");
    exit(EXIT_FAILURE);
  }
}

int strtoint(char *str) {
  int res;
  char *endptr;
  errno = 0;
  res = strtol(str, &endptr, 10);
  if (errno != 0) {
    perror("strtol");
    exit(EXIT_FAILURE);
  }
  if (endptr == str) {
    fprintf(stderr, "No digits were found\n");
    exit(EXIT_FAILURE);
  }
  return res;
}

// producer
int nurse(int id, int fd) {
  puts("nurse");
  char c;
  int nbytes;
  int novacc_this, novacc_other;
  do {
    if (sem_wait(&mem->empty) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    if (sem_wait(&mem->sem) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    nbytes = read(fd, &c, 1);
    bool eof = false;
    if (c == '1' || c == '2') {
      add_vaccine(c);
      log_nurse(id, getpid(), c, mem->novacc1, mem->novacc2);
      // if there are enough amount of vaccines, post semaphore
      novacc_this = (c == '1') ? mem->novacc1 : mem->novacc2;
      novacc_other = (c == '1') ? mem->novacc2 : mem->novacc1;
    } else {
      eof = true;
#ifdef DEBUG
      printf(C "EOF\n" T);
#endif
    }
    if (sem_post(&mem->sem) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
    if (!eof && novacc_this <= novacc_other) {
      sem_post(&mem->vaccines_available);
#ifdef DEBUG
      int val;
      sem_getvalue(&mem->vaccines_available, &val);
      printf(Y "vaccines_available: %d\n" T, val);
      fflush(stdout);
#endif
    }
  } while (nbytes != 0);
#ifdef DEBUG
  puts(R "nurse end" T);
#endif
  return 0;
}

// consumer
int vaccinator(int nocitizens, int noshots) {
  // wait until both vaccinates are available
  do {
    if (sem_wait(&mem->vaccines_available) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    /* if (mem->current_tour == noshots) */
    /*   break; */
    if (sem_wait(&mem->sem) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    // in critcal section:
    // - remove vaccines from buffer
    // - select citizen to be vaccinated
    remove_vaccines();
    int citizen_idx = mem->next_citizen_idx;
    mem->next_citizen_idx++;
    if (mem->next_citizen_idx == nocitizens) {
      mem->current_tour++;
      mem->next_citizen_idx = 0;
    }

    if (mem->next_citizen_idx)

      invite_citizen(citizen_pids[citizen_idx]);
    if (sem_post(&mem->sem) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
    if (sem_post(&mem->empty) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
  } while (true);
  /* } while (mem->current_tour != noshots); */

  /* sem_post(&mem->vaccines_available); */

#ifdef DEBUG
  puts(R "vaccinator end" T);
#endif
  return 0;
}

int citizen(int noshots) {
  printf("citizen %d\n", getpid());

  sigset_t mask;
  sigemptyset(&mask);
  sigprocmask(SIG_BLOCK, &mask, NULL);

  struct sigaction act;
  act.sa_handler = handler;
  act.sa_flags = 0;
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGUSR1);
  if (sigaction(SIGUSR1, &act, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }

  /* sigfillset(&mask); */
  /* sigdelset(&mask, SIGUSR1); */
  for (int i = 0; i < noshots; ++i) {
    // wait for only SIGUR1
    printf("waiting %d\n", i);
    sigsuspend(&mask);
  }
  /* sigprocmask(SIG_UNBLOCK, &mask, NULL); */

#ifdef DEBUG
  puts(R "citizen end" T);
  fflush(stdout);
#endif
  return 0;
}

void usage() {
  printf("Usage: ./program –n 3 –v 2 –c 3 –b 11 –t 3 –i inputfilepath\n\n"
         "n >= 2      the number of nurses (integer)\n"
         "v >= 2      the number of vaccinators (integer)\n"
         "c >= 3      the number of citizens (integer)\n"
         "b >= tc+1   size of the buffer (integer)\n"
         "t >= 1      how many times each citizen must receive the 2 shots "
         "(integer)\n"
         "i           pathname of the input file\n");
}

int main(int argc, char *argv[]) {
  int opt;
  int nonurses = -1;
  int novaccinators = -1;
  int nocitizens = -1;
  int bufsize = -1;
  int noshots = -1;
  char *inputfilepath = NULL;

  while ((opt = getopt(argc, argv, "n:v:c:b:t:i:")) != -1) {
    switch (opt) {
    case 'n':
      nonurses = strtoint(optarg);
      break;
    case 'v':
      novaccinators = strtoint(optarg);
      break;
    case 'c':
      nocitizens = strtoint(optarg);
      break;
    case 'b':
      bufsize = strtoint(optarg);
      break;
    case 't':
      noshots = strtoint(optarg);
      break;
    case 'i':
      inputfilepath = optarg;
      break;
    default:
      usage();
      exit(EXIT_FAILURE);
    }
  }

  // all arguements are required
  if (nonurses == -1 || novaccinators == -1 || nocitizens == -1 ||
      bufsize == -1 || noshots == -1 || inputfilepath == NULL) {
    usage();
    exit(EXIT_FAILURE);
  }

  if (bufsize < noshots * nocitizens + 1) {
    fprintf(stderr, "buffer size is %d, less than noshots * nocitizens + 1 \n",
            bufsize);
    usage();
    exit(EXIT_FAILURE);
  }

  log_welcome(nocitizens, noshots);
  int noprocesses = nonurses + novaccinators + nocitizens;
  // init buffer
  memsize += 2 * sizeof(int);   // vaccines
  memsize += sizeof(int);       // size of buffer
  memsize += 4 * sizeof(sem_t); // semaphores
  mem = mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
             -1, 0);
  citizen_pids = mmap(NULL, nocitizens * sizeof(int), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  init_mem(bufsize);
  if (mem == MAP_FAILED) {
    perror("mmap()");
    exit(EXIT_FAILURE);
  }

  pid_t nurse_pids[S_32];
  pid_t vaccinator_pids[S_32];

  int fd;

  fd = open(inputfilepath, O_RDONLY, 0666);
  if (fd == -1) {
    perror("open()");
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < nonurses; ++i) {
    switch (nurse_pids[i] = fork()) {
    case -1:
      perror("fork");
      exit(EXIT_FAILURE);
    case 0:
      nurse(i + 1, fd);
      exit(EXIT_SUCCESS);
    }
  }

  int c_pid;
  for (int i = 0; i < nocitizens; ++i) {
    switch (c_pid = fork()) {
    case -1:
      perror("fork");
      exit(EXIT_FAILURE);
    case 0:
      citizen(noshots);
      exit(EXIT_SUCCESS);
    default:
      citizen_pids[i] = c_pid;
    }
  }

  for (int i = 0; i < novaccinators; ++i) {
    switch (vaccinator_pids[i] = fork()) {
    case -1:
      perror("fork");
      exit(EXIT_FAILURE);
    case 0:
      vaccinator(nocitizens, noshots);
      exit(EXIT_SUCCESS);
    }
  }

  // wait for child processes to terminate
  int status;
  pid_t pid;
  for (int i = 0; i < noprocesses; ++i) {
    pid = wait(&status);
    if (pid == -1) {
      perror("wait");
    }
  }

  munmap(mem, memsize);
  munmap(citizen_pids, nocitizens * sizeof(int));

  return 0;
}
