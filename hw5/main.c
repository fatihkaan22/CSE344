#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"


// TODO: delete here
#define R "\x1B[31m"
#define C "\x1B[36m"
#define G "\x1B[32m"
#define Y "\x1B[33m"
#define T "\x1B[0m"

long money;
struct queue hws;
sem_t queue_access, queue_empty, queue_full; // TODO: consider local variable
sem_t student_available;

enum property { COST, SPEED, QUALITY };

struct student_for_hire {
  char *name;
  int q;
  int s;
  int c;
  sem_t available;
  bool busy;
};

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
    if (!s->busy) {
			return s;
    }
  }
	fprintf(stderr, "Something wrong: 1"); // TODO: 
  // all busy
  // TODO: consider wait / post semaphore
  return NULL;
}

void *thread_h(void *filepath) {
  FILE *hwfile = fopen((char *)filepath, "r");
  char c;
  while ((c = fgetc(hwfile)) != EOF) {
    if (c != 'C' && c != 'S' && c != 'Q')
      break;
    printf("H has new homework %c; remaining money is %ld\n", c, money);
    sem_wait(&queue_empty);
    sem_wait(&queue_access);
    offer(&hws, c);
    sem_post(&queue_access);
    sem_post(&queue_full);
  }
  puts("H has no other homework, terminating.");
  fclose(hwfile);
}

void *thread_student_for_hire(void *args) {
  struct student_for_hire *props = (struct student_for_hire *)args;

  while (1) {
    props->busy = false;
		printf(R "%s waiting for a homework\n" T, props->name);
    sem_wait(&props->available);
    props->busy = true;
		money -= props->c;

#ifdef DEBUG
    printf("%s %d %d %d sleeping..\n", props->name, props->s, props->q,
           props->c);
#endif
    sleep(sleep_time(props->s));

#ifdef DEBUG
    printf("%s %d %d %d done.\n", props->name, props->s, props->q, props->c);
#endif
		sem_post(&student_available);
  }
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
      case COST: // the higher the worst
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
  const int queue_size = 4;
  money = strtol(argv[3], NULL, 10);
  // TODO: check
  /* char *hwfilepath = malloc(256 * sizeof(char)); */
  /* strcpy(hwfilepath, argv[1]); */
  char *hwfilepath = argv[1];
  /* char *studentfilepath = malloc(256 * sizeof(char)); */
  /* strcpy(studentfilepath, argv[2]); */

  init_queue(&hws, queue_size);
  // TODO: check
  sem_init(&queue_access, 1, 1);
  sem_init(&queue_empty, 1, queue_size);
  sem_init(&queue_full, 1, 0);
  pthread_t h;
  if (pthread_create(&h, NULL, &thread_h, hwfilepath) != 0) { // hwfile
    perror("pthread_create()");
    exit(EXIT_FAILURE);
  }

  FILE *studentfile = fopen(argv[2], "r");

  char line[128];
  struct student_for_hire students[64]; // TODO:
  int nostudent = 0;

  // read from file
  while (fgets(line, 128, studentfile) != NULL) {
    struct student_for_hire s;
    int name_len = strcspn(line, " ") + 1;
    s.name = malloc(sizeof(char) * name_len); // TODO: check
    strncpy(s.name, line, name_len);
    char *end;
    s.q = strtol(line + name_len, &end, 10);
    s.s = strtol(end, &end, 10);
    s.c = strtol(end, &end, 10);
    students[nostudent++] = s;
  }
  fclose(studentfile);

  // sorted arrays
  sem_init(&student_available, 1, nostudent);
  int by_speed[nostudent], by_quality[nostudent], by_cost[nostudent];

  sort_by(students, by_speed, nostudent, SPEED);
  sort_by(students, by_quality, nostudent, QUALITY);
  sort_by(students, by_cost, nostudent, COST);

#ifdef DEBUG
  for (int i = 0; i < nostudent; ++i) {
    printf("s%d: %s\n", i, students[by_speed[i]].name);
    printf("q%d: %s\n", i, students[by_quality[i]].name);
    printf("c%d: %s\n", i, students[by_cost[i]].name);
  }
#endif

  // create all student threads
  pthread_t student_threads[nostudent];
  for (int i = 0; i < nostudent; ++i) {
    sem_init(&students[i].available, 1,
             0); // student will wait on this semaphore
    if (pthread_create(&student_threads[i], NULL, &thread_student_for_hire,
                       &students[i]) != 0) {
      perror("pthread_create()");
      exit(EXIT_FAILURE);
    }
  }

  while (1) {
    sem_wait(&queue_full);
    sem_wait(&queue_access);
		char hw = poll(&hws); 
    sem_post(&queue_access);
    sem_post(&queue_empty);

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
		sem_wait(&student_available); // wait if all students are busy
    struct student_for_hire *s =
        get_first_available(students, nostudent, by_priority);
		// notify student
		printf(R "%s is solving homework %c for %d. H has %ldTL left.\n" T, s->name, hw, s->c, money);
		sem_post(&s->available);
  }

  for (int i = 0; i < nostudent; ++i) {
    sem_post(&students[i].available);
  }

  for (int i = 0; i < nostudent; ++i) {
    pthread_join(student_threads[i], NULL);
  }
  if (pthread_join(h, NULL) != 0) {
    perror("pthread_join()");
    exit(EXIT_FAILURE);
  }


  puts("END");
  return 0;
}
