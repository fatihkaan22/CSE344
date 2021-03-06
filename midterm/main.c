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
int citizen_pids_size;
int *citizen_no_vaccinated;
int citizen_no_vaccinated_size;
int *buffer;
int buffer_size;

struct shared_memory {
  sem_t sem, empty, vaccines_available;
  int novacc1;
  int novacc2;
  int bufsize;
  int next_citizen_idx;
  int shots_finished;
  bool eof;
  int no_nurse_terminated;
  int remaining_citizens;
};

void handler(int signum) {
  switch (signum) {
  case SIGINT:
		write(1, "SIGINT caught, terminating\n", 27);
		exit(EXIT_SUCCESS);
    break;
  default:
    fprintf(stderr, "handler caught unexpected signal %d\n", signum);
  }
}

void exit_handler() {
  munmap(mem, memsize);
  munmap(citizen_pids, citizen_pids_size);
  munmap(citizen_no_vaccinated, citizen_no_vaccinated_size);
  munmap(buffer, buffer_size);
}

// if exist return index, -1 otherwise
bool find(int item, int *arr, int size) {
  for (int i = 0; i < size; ++i) {
    if (arr[i] == item)
      return i;
  }
  return -1;
}

char *get_ordinal(int value) {
  char *ordinals[] = {"st", "nd", "rd", "th"};
  value %= 100;
  if (3 < value && value < 21)
    return ordinals[3];
  switch (value % 10) {
  case 1:
    return ordinals[0];
  case 2:
    return ordinals[1];
  case 3:
    return ordinals[2];
  default:
    return ordinals[3];
  }
}

void log_nurse(int id, int pid, char vaccine, int total1, int total2) {
#ifdef DEBUG
  printf(Y);
#endif
  printf("Nurse %d (pid=%d) has brought vaccine %c: the clinic has %d vaccine1 "
         "and %d vaccine2.\n",
         id, pid, vaccine, total1, total2);
#ifdef DEBUG
  printf(T);
#endif
  fflush(stdout);
}

void log_nurse_done() {
  puts("Nurses have carried all vaccines to the buffer, terminating.");
  fflush(stdout);
}

void log_citizen(int id, int pid, int times, int vacc1, int vacc2,
                 int remaining) {
#ifdef DEBUG
  printf(C);
#endif
  if (remaining == -1)
    printf(
        "Citizen %d (pid=%d) is vaccinated for the %d%s time: the clinic has "
        "%d vaccine1 and %d vaccine2. \n",
        id, pid, times, get_ordinal(times), vacc1, vacc2);
  else
    printf(
        "Citizen %d (pid=%d) is vaccinated for the %d%s time: the clinic has "
        "%d vaccine1 and %d vaccine2. The citizen is leaving. Remaining "
        "citizens to vaccinate: %d\n",
        id, pid, times, get_ordinal(times), vacc1, vacc2, remaining);
#ifdef DEBUG
  printf(T);
#endif
  fflush(stdout);
}

void log_citizen_done() {
  puts("All citizens have been vacinated.");
  fflush(stdout);
}

void log_welcome(int c, int t) {
  printf("Welcome to GTU344 clinic. Number of citizens to vaccinate "
         "c=%d with t=%d doeses.\n",
         c, t);
  fflush(stdout);
}

void log_vaccinator_done(int id, int pid, int nodose) {
  printf("Vaccinator %d (pid=%d) vaccinated %d doses.\n", id, pid, nodose);
  fflush(stdout);
}

void log_vaccinator(int id, int pid, int citizen_pid) {
#ifdef DEBUG
  printf(G);
#endif
  printf("Vaccinator %d (pid=%d) is inviting citizen pid=%d to the clinic.\n",
         id, pid, citizen_pid);
#ifdef DEBUG
  printf(T);
#endif
  fflush(stdout);
}

int add_vaccine(char c) {
  for (int i = 0; i < buffer_size; ++i) {
    if (buffer[i] == EMPTY)
      buffer[i] = c;
  }
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
  for (int i = 0; i < buffer_size; ++i)
    if (buffer[i] == '1') {
      buffer[i] = EMPTY;
      break;
    }
  for (int i = 0; i < buffer_size; ++i)
    if (buffer[i] == '2') {
      buffer[i] = EMPTY;
      break;
    }
  mem->novacc1--;
  mem->novacc2--;
  return 0;
}

void init_mem(int bufsize, int nocitizens) {
  mem->bufsize = bufsize;
  mem->novacc1 = mem->novacc2 = 0;
  mem->next_citizen_idx = 0;
  mem->eof = false;
  mem->no_nurse_terminated = 0;
  mem->remaining_citizens = nocitizens;
  if (sem_init(&mem->sem, 1, 1) == -1) {
    perror("sem_init");
    exit(EXIT_FAILURE);
  }
  if (sem_init(&mem->empty, 1, bufsize) == -1) {
    perror("sem_init");
    exit(EXIT_FAILURE);
  }
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
  char c;
  int nbytes;
  int novacc_this, novacc_other;
  while (!mem->eof) {
    if (sem_wait(&mem->empty) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    if (mem->eof)
      break;
    if (sem_wait(&mem->sem) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    nbytes = read(fd, &c, 1);
    if (nbytes == -1) {
      perror("read");
      exit(EXIT_FAILURE);
    } else if (nbytes == 0) {
      mem->eof = true;
    } else if (c == '1' || c == '2') {
      add_vaccine(c);
      log_nurse(id, getpid(), c, mem->novacc1, mem->novacc2);
      // if there are enough amount of vaccines, post semaphore
      novacc_this = (c == '1') ? mem->novacc1 : mem->novacc2;
      novacc_other = (c == '1') ? mem->novacc2 : mem->novacc1;
    }

    if (sem_post(&mem->sem) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
    if (!mem->eof && novacc_this <= novacc_other) {
      if (sem_post(&mem->vaccines_available) == -1) {
        perror("sem_post()");
        exit(EXIT_FAILURE);
      }
    }
  }
  if (sem_post(&mem->empty) == -1) {
    perror("sem_post()");
    exit(EXIT_FAILURE);
  }
  /* #ifdef DEBUG */
  /*   printf(R "nurse %d terminated\n" T, id); */
  /* #endif */
  mem->no_nurse_terminated++;
  return 0;
}

// consumer
int vaccinator(int id, int nocitizens, int noshots,
               int c_pipes[nocitizens][2]) {
  int nodose = 0, v1, v2, remaining, c_no_vacc;
  char msg = 'x'; // message to send from fifo
  // wait until both vaccinates are available
  while (mem->shots_finished > 0) {
    if (sem_wait(&mem->vaccines_available) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    if (mem->shots_finished <= 0)
      break;
    if (sem_wait(&mem->sem) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    if (mem->shots_finished <= 0)
      break;
    // in critcal section:
    // - remove vaccines from buffer
    // - select citizen to be vaccinated
    mem->shots_finished--;
    remove_vaccines();
    v1 = mem->novacc1;
    v2 = mem->novacc2;
    int citizen_idx = mem->next_citizen_idx;
    mem->next_citizen_idx++;
    if (mem->next_citizen_idx == nocitizens) {
      mem->next_citizen_idx = 0;
    }
    c_no_vacc = ++citizen_no_vaccinated[citizen_idx];
    if (citizen_no_vaccinated[citizen_idx] == noshots)
      remaining = --mem->remaining_citizens;
    else
      remaining = -1;
    log_vaccinator(id, getpid(), citizen_pids[citizen_idx]);
    log_citizen(citizen_idx + 1, citizen_pids[citizen_idx], c_no_vacc, v1, v2,
                remaining);
    if (sem_post(&mem->sem) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
    if (sem_post(&mem->empty) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }

    // invite citizen
    if (write(c_pipes[citizen_idx][1], &msg, sizeof(char)) == -1) {
      perror("write()");
      exit(EXIT_FAILURE);
    }

    nodose++;
  }

  sem_post(&mem->vaccines_available);

  /* #ifdef DEBUG */
  /*   printf(R "vaccinator %d terminated\n" T, id); */
  /* #endif */
  log_vaccinator_done(id, getpid(), nodose);
  return 0;
}

int citizen(int id, int noshots, int c_pipes[2]) {
  char msg;
  for (int i = 0; i < noshots; ++i) {
    read(c_pipes[0], &msg, sizeof(char));
  }

  if (close(c_pipes[0]) == -1)
    perror("close");
  /* #ifdef DEBUG */
  /*   printf(R "citizen %d terminated\n" T, id); */
  /*   fflush(stdout); */
  /* #endif */
  return 0;
}

void usage() {
  printf("Usage: ./program ???n 3 ???v 2 ???c 3 ???b 11 ???t 3 ???i inputfilepath\n\n"
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
	signal(SIGINT, handler);
  atexit(exit_handler);
  // init buffer
  memsize = sizeof(struct shared_memory); // vaccines
  mem = mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
             -1, 0);
  citizen_pids_size = nocitizens * sizeof(int);
  citizen_pids = mmap(NULL, citizen_pids_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  citizen_no_vaccinated_size = nocitizens * sizeof(int);
  citizen_no_vaccinated =
      mmap(NULL, citizen_no_vaccinated_size, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  buffer_size = bufsize * sizeof(char);
  buffer = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  init_mem(bufsize, nocitizens);
	mem->shots_finished = noshots * nocitizens;
  if (mem == MAP_FAILED) {
    perror("mmap()");
    exit(EXIT_FAILURE);
  }

  pid_t nurse_pids[nonurses];
  pid_t vaccinator_pids[novaccinators];

  int fd = open(inputfilepath, O_RDONLY, 0666);
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

  int c_pipes[nocitizens][2];
  int c_pid;
  for (int i = 0; i < nocitizens; ++i) {
    citizen_no_vaccinated[i] = 0;
    if (pipe(c_pipes[i]) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
    switch (c_pid = fork()) {
    case -1:
      perror("fork");
      exit(EXIT_FAILURE);
    case 0:
      if (close(c_pipes[i][1]) == -1) {
        perror("close()");
        exit(EXIT_FAILURE);
      }
      citizen(i + 1, noshots, c_pipes[i]);
      exit(EXIT_SUCCESS);
    default:
      if (close(c_pipes[i][0]) == -1) {
        perror("close()");
        exit(EXIT_FAILURE);
      }
      citizen_pids[i] = c_pid;
    }
  }

  for (int i = 0; i < novaccinators; ++i) {
    switch (vaccinator_pids[i] = fork()) {
    case -1:
      perror("fork");
      exit(EXIT_FAILURE);
    case 0:
      vaccinator(i + 1, nocitizens, noshots, c_pipes);
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
    } else if (mem->no_nurse_terminated == nonurses) {
      mem->no_nurse_terminated++; // to avoid print multiple times
      log_nurse_done();
    } else if (mem->remaining_citizens == 0) {
      mem->remaining_citizens--; // to avoid print multiple times
      log_citizen_done();
    }
  }

  puts("The clinic is now close. Stay healthy.");

  for (int i = 0; i < nocitizens; ++i) {
    if (close(c_pipes[i][1]) == -1)
      perror("close");
  }
  munmap(mem, memsize);
  munmap(citizen_pids, citizen_pids_size);
  munmap(citizen_no_vaccinated, citizen_no_vaccinated_size);
  munmap(buffer, buffer_size);

  return 0;
}
