#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"

// constants
#define NO_HW_LEFT 'N'
#define MAX_STUDENT 64
#define MAX_LINE_LEN 128
#define QUEUE_SIZE 4

static volatile sig_atomic_t gotSigint = 0;
long money;
struct queue hws;
sem_t queue_access, queue_empty, queue_full;
sem_t student_available;
sem_t thread_h_term;
bool terminate;

enum property { COST, SPEED, QUALITY };

struct student_for_hire {
  char *name;
  int q;
  int s;
  int c;
  sem_t available;
  sem_t sem_busy;
  bool busy;
  int no_hw_solved;
};

static void handler(int sig) {
  if (sig == SIGINT)
    gotSigint = 1;
}

void usage() {
  printf("Usage: ./program <homeworkFilePath> <studentsFilePath> <money>\n");
}

int sleep_time(int speed) { return 6 - speed; }

enum property get_enum(char c) {
  switch (c) {
  case 'C':
    return COST;
  case 'S':
    return SPEED;
  case 'Q':
    return QUALITY;
  }
  return -1;
}

struct student_for_hire *get_first_available(struct student_for_hire *arr,
                                             int nostudent, int *priorities) {
  for (int i = 0; i < nostudent; ++i) {
    struct student_for_hire *s = &arr[priorities[i]];
    sem_wait(&s->sem_busy);
    bool busy = !s->busy;
    sem_post(&s->sem_busy);
    if (busy) {
      return s;
    }
  }
  // all busy
  // sanity check
  fprintf(stderr, "Something went wrong: waiting on semaphore "
                  "'student_available' doesn't work.");
  exit(EXIT_FAILURE);
  return NULL;
}

void *thread_h(void *filepath) {
  if (pthread_detach(pthread_self()) != 0) {
    fprintf(stderr, "pthread_detach error\n");
  }
  FILE *hwfile = fopen((char *)filepath, "r");
  char c;
  while ((c = fgetc(hwfile)) != EOF) {
    if (c != 'C' && c != 'S' && c != 'Q')
      break;
    sem_wait(&queue_empty);
    sem_wait(&queue_access);
    if (terminate)
      break;
    offer(&hws, c);
    sem_post(&queue_access);
    sem_post(&queue_full);
    printf("H has new homework %c; remaining money is %ld\n", c, money);
  }

  if (!terminate) {
    // notify no homeworks left
    sem_wait(&queue_empty);
    sem_wait(&queue_access);
    offer(&hws, NO_HW_LEFT);
    sem_post(&queue_access);
    sem_post(&queue_full);
    puts("H has no other homework, terminating.");
  }

  if (fclose(hwfile) == -1) {
    perror("fclose()");
  }
  sem_post(&thread_h_term);
  return NULL;
}

void *thread_student_for_hire(void *args) {
  struct student_for_hire *s = (struct student_for_hire *)args;

  while (1) {
    if (!terminate && !s->busy)
      printf("%s waiting for a homework\n", s->name);
    sem_wait(&s->available);
    if (terminate)
      break;
    sleep(sleep_time(s->s));
    sem_wait(&s->sem_busy);
    s->busy = false;
    sem_post(&s->sem_busy);
    sem_post(&student_available);
  }
#ifdef DEBUG
  printf("%s end\n", s->name);
#endif
  return NULL;
}

void sort_by(struct student_for_hire *students, int *res, int size,
             enum property p) {
  for (int i = 0; i < size; ++i) {
    res[i] = -1;
  }
  for (int i = 0; i < size; ++i) {
    int idx = 0;
    for (int j = 0; j < size; ++j) {
      if (i == j)
        continue;
      int val_i, val_j;
      switch (p) {
      case SPEED:
        val_i = students[i].s;
        val_j = students[j].s;
        break;
      case COST: // the higher the less priority
        val_i = students[j].c;
        val_j = students[i].c;
        break;
      case QUALITY:
        val_i = students[i].q;
        val_j = students[j].q;
        break;
      }
      if (val_i < val_j)
        idx++;
    }
    while (res[idx] != -1)
      idx++;
    res[idx] = i;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Invalid number of arguements\n");
    usage();
    exit(EXIT_SUCCESS);
  }

  // signal handler
  signal(SIGINT, &handler);

  const int queue_size = QUEUE_SIZE;
  char *endptr;
  money = strtol(argv[3], &endptr, 10); // TODO: check return
  if (endptr == argv[3]) {
    fprintf(stderr, "Error: couldn't parse to int: %s\n", argv[3]);
    exit(EXIT_SUCCESS);
  }
  long initial_money = money;
  char *hwfilepath = argv[1];
  init_queue(&hws, queue_size);
  // TODO: check
  if (sem_init(&queue_access, 0, 1) == -1) {
    perror("sem_init()");
    exit(EXIT_FAILURE);
  }
  if (sem_init(&queue_empty, 0, queue_size) == -1) {
    perror("sem_init()");
    exit(EXIT_FAILURE);
  }
  if (sem_init(&queue_full, 0, 0) == -1) {
    perror("sem_init()");
    exit(EXIT_FAILURE);
  }
  if (sem_init(&thread_h_term, 0, 0) == -1) {
    perror("sem_init()");
    exit(EXIT_FAILURE);
  }

  pthread_t h;
  if (pthread_create(&h, NULL, &thread_h, hwfilepath) != 0) { // hwfile
    perror("pthread_create()");
    exit(EXIT_FAILURE);
  }

  FILE *studentfile = fopen(argv[2], "r");
  if (studentfile == NULL) {
    fprintf(stderr, "Cannot open file: %s\n", argv[2]);
    exit(EXIT_FAILURE);
  }

  char line[MAX_LINE_LEN]; // max length of one line
  struct student_for_hire students[MAX_STUDENT];
  int nostudent = 0;

  // read from file
  while (fgets(line, 128, studentfile) != NULL) {
    struct student_for_hire s;
    int name_len = strcspn(line, " ") + 1;
    s.name = malloc(sizeof(char) * name_len); // TODO: check
    if (s.name == NULL) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    strncpy(s.name, line, name_len);
    s.name[name_len - 1] = '\0';
    char *end;
    s.q = strtol(line + name_len, &end, 10);
    s.s = strtol(end, &end, 10);
    s.c = strtol(end, &end, 10);
    students[nostudent++] = s;
  }
  if (fclose(studentfile) == -1) {
    perror("fclose()");
  }

  if (sem_init(&student_available, 0, nostudent) == -1) {
    perror("sem_init()");
    exit(EXIT_FAILURE);
  }

  // create all student threads
  pthread_t student_threads[nostudent];
  for (int i = 0; i < nostudent; ++i) {
    if (sem_init(&students[i].sem_busy, 0, 1) == -1) {
      perror("sem_init()");
      exit(EXIT_FAILURE);
    }
    students[i].no_hw_solved = 0;
    students[i].busy = false;
    // student will wait on this semaphore
    if (sem_init(&students[i].available, 0, 0) == -1) {
      perror("sem_init()");
      exit(EXIT_FAILURE);
    }

    if (pthread_create(&student_threads[i], NULL, &thread_student_for_hire,
                       &students[i]) != 0) {
      perror("pthread_create()");
      exit(EXIT_FAILURE);
    }
  }

  // print students
  printf("%d students-for-hire threads have been created.\n", nostudent);
  puts("Name Q S C");
  for (int i = 0; i < nostudent; ++i) {
    printf("%s %d %d %d\n", students[i].name, students[i].q, students[i].s,
           students[i].c);
  }

  // sorted arrays
  int by_speed[nostudent], by_quality[nostudent], by_cost[nostudent];

  sort_by(students, by_speed, nostudent, SPEED);
  sort_by(students, by_quality, nostudent, QUALITY);
  sort_by(students, by_cost, nostudent, COST);

  int total_hws = 0;
  while (1) {
    if (gotSigint) {
      puts("Termination signal received, closing.");
      break;
    }

    if (sem_wait(&queue_full) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    if (sem_wait(&queue_access) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    char hw = poll(&hws);
    if (sem_post(&queue_access) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
    if (sem_post(&queue_empty) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }

    if (hw == NO_HW_LEFT) {
      puts("No more homeworks left or coming in, closing.");
      break;
    }

    int *by_priority;
    switch (get_enum(hw)) {
    case SPEED:
      by_priority = by_speed;
      break;
    case COST:
      by_priority = by_cost;
      break;
    case QUALITY:
      by_priority = by_quality;
    }
    // wait if all students are busy
    if (sem_wait(&student_available) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }

    struct student_for_hire *s =
        get_first_available(students, nostudent, by_priority);
    // notify student
    /* sem_wait(&sem_money); */
    if (money < s->c) {
      puts("H has no more money for homeworks, terminating.");
      break;
    }
    money -= s->c;
    /* sem_post(&sem_money); */
    printf("%s is solving homework %c for %d. H has %ldTL left.\n", s->name, hw,
           s->c, money);
    s->no_hw_solved++;

    if (sem_wait(&s->sem_busy) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }
    s->busy = true;
    if (sem_post(&s->sem_busy) == -1) {
      perror("sem_wait()");
      exit(EXIT_FAILURE);
    }

    if (sem_post(&s->available) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }

    total_hws++;
  }

  terminate = true;

  for (int i = 0; i < nostudent; ++i) {
    if (sem_post(&students[i].available) == -1) {
      perror("sem_post()");
      exit(EXIT_FAILURE);
    }
  }
  if (sem_post(&queue_empty) == -1) {
    perror("sem_post()");
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < nostudent; ++i) {
    if (pthread_join(student_threads[i], NULL) != 0) {
      fprintf(stderr, "Error: pthread_join\n");
      exit(EXIT_FAILURE);
    }
  }

  puts("Homeworks sovled and money made by the students:");
  int sum = 0;
  int hwsum = 0;
  for (int i = 0; i < nostudent; ++i) {
    printf("%s %d %d\n", students[i].name, students[i].no_hw_solved,
           students[i].no_hw_solved * students[i].c);
    sum += students[i].c * students[i].no_hw_solved;
    hwsum += students[i].no_hw_solved;
  }

  printf("Total cost for %d homeworks %ldTL.\n", total_hws,
         initial_money - money);
  printf("Total cost for %d homeworks %dTL.\n", hwsum, sum);
  printf("Money left at H's account: %ldTL.\n", money);

  // free
  for (int i = 0; i < nostudent; ++i) {
    free(students[i].name);
    if (sem_destroy(&students[i].available) == -1)
      perror("sem_destroy");
    if (sem_destroy(&students[i].sem_busy) == -1)
      perror("sem_destroy");
  }
  if (sem_destroy(&queue_empty) == -1)
    perror("sem_destroy");
  if (sem_destroy(&queue_access) == -1)
    perror("sem_destroy");
  if (sem_destroy(&queue_full) == -1)
    perror("sem_destroy");
  if (sem_destroy(&student_available) == -1)
    perror("sem_destroy");
  free_queue(&hws);

  // wait for h to terminate
  if (sem_wait(&thread_h_term) == -1) {
    perror("sem_wait()");
    exit(EXIT_FAILURE);
  }
  return 0;
}
