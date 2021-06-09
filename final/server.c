#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "csv_reader.h"
#include "query.h"
#include "queue.h"
#include "socket_io.h"

#define R "\x1B[31m"
#define C "\x1B[36m"
#define G "\x1B[32m"
#define Y "\x1B[33m"
#define T "\x1B[0m"

struct thread_pool {
  pthread_t *threads;
  pthread_cond_t cond;
  pthread_mutex_t lock;
  int size;
};

pthread_mutex_t rw_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ok_to_read = PTHREAD_COND_INITIALIZER;
pthread_cond_t ok_to_write = PTHREAD_COND_INITIALIZER;

int no_active_readers, no_active_writers, no_waiting_readers,
    no_waiting_writers;

unsigned int no_busy;

struct thread_pool pool;
struct queue client_queue;
FILE *flog;

void init_thread_pool(int size) {
  // TODO: check
  pool.threads = malloc(size * sizeof(pthread_t));
  pool.size = size;
  pthread_cond_init(&pool.cond, NULL);
  pthread_mutex_init(&pool.lock, NULL);
  no_busy = size;
}

void usage() {
  printf("Usage: ./server -p port -o path/to/logfile -l poolsize -d "
         "path/to/dataset.csv\n"
         " -p port       port number the server will use for incoming "
         "connections\n"
         " -o logfile    path of the log file to which the server daemon will "
         "write\n"
         " -l poolsize   the number of threads in the pool (>= 2) \n"
         " -d dataset    path of a csv file containing a single table\n");
  exit(EXIT_FAILURE);
}

int try_parse_int(char *optarg) {
  int result;
  char *endptr;
  errno = 0;
  result = strtol(optarg, &endptr, 10);
  if (errno != 0) {
    perror("strtol");
    usage();
  }
  if (endptr == optarg) {
    fprintf(stderr, "No digits were found\n");
    usage();
  }
  return result;
}

void send_error_msg(int client_fd, const char *request) {
  send_int(0, client_fd);
  char msg[1024];
  sprintf(msg, "ERROR: Couldn't parse/run query: '%s'", request);
  send_line(msg, strlen(msg) + 1, client_fd);
}

void send_result(int client_fd, char **columns, int rows[], int rows_size) {
  if (rows_size == 0)
    return;
  bool include[table.no_cols];
  set_include_cols(include, columns);

  char buffer[1024];
  int len;
  // first send how many lines the data is
  // TODO: send integer
  send_int(rows_size, client_fd);

  len = sprint_row(buffer, table.header, include);
  /* printf("%s\n", buffer); */
  send_line(buffer, len + 1, client_fd);
  /* send(client_fd, buffer, len + 1, 0); */
  /* print_row(table.header, include); */
  for (int i = 0; i < rows_size; ++i) {
    len = sprint_row(buffer, table.records[rows[i]], include);
    send_line(buffer, len + 1, client_fd);
    /* printf("%s\n", buffer); */
    /* send(client_fd, buffer, 1024, 0); */
  }
}

void process_query(int id, int client_fd, query q, const char *request) {
  int result_records[table.size + 1]; // TODO:
  unsigned int res_size;
  if (run_query(q, result_records, &res_size) == -1) {
    fprintf(stderr, "Error while running query: '%s'\n", request);
  } else {
    /* print_table(q.columns, result_records, res_size); */
    send_result(client_fd, q.columns, result_records, res_size);
    print_timestamp(flog);
    fprintf(flog,
            "Thread #%d: query completed, %d records have been returned.\n", id,
            res_size);
  }
}

void reader(int id, int client_fd, query q, const char *request) {
  // wait until no writers
  pthread_mutex_lock(&rw_lock);
  while ((no_active_writers + no_waiting_writers) > 0) {
    no_waiting_writers++;
    pthread_cond_wait(&ok_to_read, &rw_lock);
    no_waiting_writers--;
  }
  no_active_readers++;
  pthread_mutex_unlock(&rw_lock);
  // access database
  process_query(id, client_fd, q, request);
  // check out -- wake up waiting writer
  pthread_mutex_lock(&rw_lock);
  no_active_readers--;
  if (no_active_readers == 0 && no_waiting_writers > 0) {
    pthread_cond_signal(&ok_to_write);
  }
  pthread_mutex_unlock(&rw_lock);
}

void writer(int id, int client_fd, query q, const char *request) {
  // wait until no readers or writers
  pthread_mutex_lock(&rw_lock);
  while ((no_active_writers + no_active_readers) > 0) {
    no_waiting_writers++;
    pthread_cond_wait(&ok_to_write, &rw_lock);
    no_waiting_writers--;
  }
  no_active_writers++;
  pthread_mutex_unlock(&rw_lock);
  // access database
  process_query(id, client_fd, q, request);
  // check out -- wake up waiting reader or  writer
  pthread_mutex_lock(&rw_lock);
  no_active_writers--;
  if (no_waiting_writers > 0)
    pthread_cond_signal(&ok_to_write);
  else if (no_waiting_readers > 0)
    pthread_cond_broadcast(&ok_to_read);
  pthread_mutex_unlock(&rw_lock);
}

void *thread(void *args) {
  int id =  *((int *) args);
  free(args);

  char request[1024];

  // wait for work
  while (true) {
    pthread_mutex_lock(&pool.lock);
    no_busy--;
    print_timestamp(flog);
    fprintf(flog, "Thread #%d: waiting for connection\n", id);
    while (client_queue.size == 0) {
      pthread_cond_wait(&pool.cond, &pool.lock);
    }
    print_timestamp(flog);
    fprintf(flog, "A connection has been delegated to thread id #%d\n", id);
    int client_fd = poll(&client_queue);
    no_busy++;
    /* fprintf(flog, "client_fd: %d\n", client_fd); */
    pthread_mutex_unlock(&pool.lock);

    while (true) {
      /* int nobytes = recv(client_fd, request, 1024, 0); */
      int res = receive_line(request, client_fd);

      if (res == -1) {
        perror("recv()");
        exit(EXIT_FAILURE);
      }
      if (res == 1) {
        puts("nobytes 0");
        break;
      }
      print_timestamp(flog);
      fprintf(flog, "Thread #%d: received query '%s'\n", id, request);

      char *query_str = malloc(1024);
      strcpy(query_str, request);
      query q;
      if (parse_query(query_str, &q) == -1) {
        print_timestamp(flog);
        fprintf(flog, "Error while parsing query: '%s'\n", request);
        send_error_msg(client_fd, request);
        continue;
      }
#ifdef DEBUG
      /* print_query(q); */
#endif
      if (query_select(q.cmd)) {
        reader(id, client_fd, q, request);
      } else if (query_update(q.cmd)) {
        writer(id, client_fd, q, request);
      } else {
        print_timestamp(flog);
        fprintf(flog, "ERROR: Unknown query\n");
      }

      sleep(1); // FIX: 0.5
    }
  }
  return NULL;
}

int init_socket(size_t port) {
  int server_fd;
  struct sockaddr_in server_addr;

  /* if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) { */
  /*   perror("signal"); */
  /* } */

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, (int[]){1},
                 sizeof(int)) == -1) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr,
           sizeof(struct sockaddr_in)) == -1) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, SOMAXCONN) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  return server_fd;
}

int become_daemon() {
}

int main(int argc, char *argv[]) {
  int opt;
  uint16_t port = 0;
  unsigned int poolsize = 0;
  char *logfile_path = NULL;
  char *dataset_path = NULL;

  while ((opt = getopt(argc, argv, "p:o:l:d:")) != -1) {
    switch (opt) {
    case 'p':
      // TODO: maybe don't need to parse
      port = try_parse_int(optarg);
      break;
    case 'o':
      logfile_path = optarg;
      break;
    case 'l':
      poolsize = try_parse_int(optarg);
      if (poolsize < 2) {
        print_timestamp(flog);
        fprintf(flog,
                "ERROR: poolsize need to be larger or equal to 2, got: %d\n",
                poolsize);
        usage();
      }
      break;
    case 'd':
      dataset_path = optarg;
      break;
    default:
      usage();
    }
  }

  if (port == 0 || logfile_path == NULL || poolsize == 0 ||
      dataset_path == NULL) {
    usage();
  }

  /* int logfile_fd = open(logfile_path, O_WRONLY | O_CREAT, 0644); */
  /* if (logfile_fd == -1) { */
  /*   perror("open()"); */
  /*   exit(EXIT_FAILURE); */
  /* } */

  /* setbuf(stdout, NULL); */
  /* if (dup2(logfile_fd, 1) == -1) { */
  /*   perror("dup2()"); */
  /*   exit(EXIT_FAILURE); */
  /* } */
  /* setbuf(stdout, NULL); */

  flog = fopen(logfile_path, "w");
  if (flog == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }
  // disable buffering
  setbuf(flog, NULL);

  // load dataset into memory
  FILE *fp = fopen(dataset_path, "r");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }
  print_timestamp(flog);
  fprintf(flog, "Loading dataset...\n"); // TODO:

  /* time_t start, end; */
  /* time_t rawtime; */
  /* struct tm * timeinfo; */
  /* time (&rawtime); */
  /* timeinfo = localtime (&rawtime); */
  /* printf ("%s", asctime(timeinfo)); */

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  read_csv(fp);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  print_timestamp(flog);
  fprintf(flog, "Dataset loaded in ");
  print_time_diff(start, end);
  fprintf(flog, " seconds with %ld records.\n", table.size);

  no_active_readers = no_active_writers = no_waiting_readers =
      no_waiting_writers = 0;

  int server_fd = init_socket(port);
  // threads will wait on queue size, so initialize beforehand
  init_queue(&client_queue, 32);
  // create threads
  init_thread_pool(poolsize);
  print_timestamp(flog);
  fprintf(flog, "A pool of %d threads has been created\n", poolsize);
  for (int i = 0; i < poolsize; ++i) {
    int *arg = malloc(sizeof(*arg));
    *arg = i;
    if (pthread_create(&pool.threads[i], NULL, &thread, arg) != 0) {
      perror("pthread_create()");
      exit(EXIT_FAILURE);
    }
  }

  struct sockaddr_in client_addr;
  // TODO: change addrlen
  socklen_t addrlen = sizeof(struct sockaddr_in);

  while (true) {
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (client_fd == -1) {
      perror("accept()");
      exit(EXIT_FAILURE);
    }

    // add client to queue
    pthread_mutex_lock(&pool.lock);
    if (no_busy == poolsize) {
      print_timestamp(flog);
      fprintf(flog, "No thread available! Waiting...\n");
    }
    offer(&client_queue, client_fd);
    pthread_cond_broadcast(&pool.cond);
    pthread_mutex_unlock(&pool.lock);
  }

  for (size_t i = 0; i < poolsize; ++i) {
    if (pthread_join(pool.threads[i], NULL) != 0) {
      perror("pthread_join()");
      exit(EXIT_FAILURE);
    }
  }

  return 0;

  /* cmd = "SELECT Period, Magnitude FROM TABLE WHERE " */
  /*       "Series_reference='BDCQ.SEA1AA';"; */
  /* cmd = "SELECT * FROM TABLE WHERE " */
  /*       "STATUS='F',Series_reference='BDCQ.SEA1AA',Period='2020.12';"; */
  /* cmd = "SELECT columnName1, columnName2, columnName3 FROM TABLE;"; */
  /* cmd = "UPDATE TABLE SET Period='-1' WHERE Series_reference='BDCQ.SEA1AA';";
   */
  /* cmd = "UPDATE TABLE SET columnName1='value1';"; */
  /* input = "SELECT DISTINCT Series_reference,Series_title_5 FROM TABLE;"; */

  /* char *query_str = malloc(256); */
  /* strcpy(query_str, input); */

  /* query q; */
  /* memset(&q, 0, sizeof(q)); */
  /* if (parse_query(query_str, &q) == -1) { */
  /*   fprintf(stderr, "Error while parsing query: '%s'\n", input); */
  /* } */

  /* #ifdef DEBUG */
  /* print_query(q); */
  /* #endif */

  /* if (run_query(q) == -1) { */
  /*   fprintf(stderr, "Error while running query: '%s'\n", input); */
  /* } */

  return 0;
}
