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
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

#define PIDFILE "/tmp/cse344-171044009.pid"
#define BD_MAX_CLOSE 8192
#define SLEEP 500000

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

static volatile sig_atomic_t got_sigint = 0;

static void handler(int sig) {
  if (sig == SIGINT)
    got_sigint = 1;
}

void init_thread_pool(int size) {
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
  send_int(rows_size, client_fd);

  len = sprint_row(buffer, table.header, include);
  /* printf("%s\n", buffer); */
  send_line(buffer, len + 1, client_fd);
  /* send(client_fd, buffer, len + 1, 0); */
  /* print_row(table.header, include); */
  for (int i = 0; i < rows_size; ++i) {
    len = sprint_row(buffer, table.records[rows[i]], include);
    send_line(buffer, len + 1, client_fd);
  }
}

void process_query(int id, int client_fd, query q, const char *request) {
  int result_records[table.size + 1];
  unsigned int res_size;
  usleep(SLEEP);
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
  int id = *((int *)args);
  free(args);

  char request[1024];
  int client_fd;

  // wait for work
  while (true) {
    pthread_mutex_lock(&pool.lock);
    no_busy--;
    print_timestamp(flog);
    fprintf(flog, "Thread #%d: waiting for connection\n", id);
    while (client_queue.size == 0) {
      if (got_sigint) {
        pthread_mutex_unlock(&pool.lock);
        return 0;
      }
      pthread_cond_wait(&pool.cond, &pool.lock);
    }
    print_timestamp(flog);
    fprintf(flog, "A connection has been delegated to thread id #%d\n", id);
    client_fd = poll(&client_queue);
    no_busy++;
    /* fprintf(flog, "client_fd: %d\n", client_fd); */
    pthread_mutex_unlock(&pool.lock);

    if (got_sigint)
      break;

    while (true) {
      int res = receive_line(request, client_fd);

      if (res == -1) {
        perror("recv()");
        exit(EXIT_FAILURE);
      }
      if (res == 1) {
        fprintf(stderr, "No bytes read, client might be closed\n");
        break;
      }
      print_timestamp(flog);
      fprintf(flog, "Thread #%d: received query '%s'\n", id, request);

      char *query_str = malloc(4096);
      strcpy(query_str, request);
      query q;
      if (parse_query(query_str, &q) == -1) {
        free(query_str);
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
      free(query_str);

    }
  }
  return 0;
}

int init_socket(size_t port) {
  int server_fd;
  struct sockaddr_in server_addr;

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    perror("signal");
  }

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
  int maxfd, fd;
  switch (fork()) {
  case -1:
    return -1;
  case 0:
    break;
  default:
    exit(EXIT_SUCCESS);
  }
  if (setsid() == -1)
    return -1;
  switch (fork()) {
  case -1:
    return -1;
  case 0:
    break;
  default:
    exit(EXIT_SUCCESS);
  }
  umask(0);

  maxfd = sysconf(_SC_OPEN_MAX);
  if (maxfd == -1)
    maxfd = BD_MAX_CLOSE;
  for (fd = 0; fd < maxfd; fd++)
    close(fd);

  close(STDIN_FILENO);
  fd = open("/dev/null", O_RDWR);
  if (fd != STDIN_FILENO)
    return -1;
  if (dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    return -1;
  if (dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO)
    return -1;
  return 0;
}

int single_instance() {
  int buflen = 100;
  char buf[buflen];
  int fd = open(PIDFILE, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    perror("open pid file");
    exit(EXIT_FAILURE);
  }

  struct flock fl;
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;

  if (fcntl(fd, F_SETLK, &fl) == -1) {
    if (errno == EAGAIN || errno == EACCES) {
      fprintf(stderr, "PID file %s is locked, instance already running\n",
              PIDFILE);
      return -1;
    }
    perror("fcntl");
    exit(EXIT_FAILURE);
  }

  if (ftruncate(fd, 0) == -1) {
    perror("ftruncate()");
    exit(EXIT_FAILURE);
  }

  snprintf(buf, buflen, "%ld\n", (long)getpid());
  if (write(fd, buf, strlen(buf)) != strlen(buf)) {
    perror("write()");
    exit(EXIT_FAILURE);
  }
  return 0;
}


void exit_handler() {
  fclose(flog);
  free_queue(&client_queue);

  for (int i = 0; i < table.no_cols; ++i) {
    free(table.header[i]);
  }
  free(table.header);

  for (int i = 0; i < table.size; ++i) {
    for (int j = 0; j < table.no_cols; ++j) {
      free(table.records[i][j]);
    }
    free(table.records[i]);
  }
  free(table.records);

  pthread_cond_destroy(&pool.cond);
  pthread_mutex_destroy(&pool.lock);
  free(pool.threads);
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

  FILE *fp = fopen(dataset_path, "r");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }

  if (single_instance() != 0)
    exit(EXIT_FAILURE);
  become_daemon();
  if (single_instance() != 0) 
    exit(EXIT_FAILURE);

  // signal handler
  struct sigaction sa;
  /* memset(&sa, 0, sizeof(sa)); */
  sa.sa_handler = &handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction()");
  }

  if (atexit(exit_handler) == -1)
    perror("atexit");


  flog = fopen(logfile_path, "w");
  if (flog == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }
  // disable buffering
  setbuf(flog, NULL);

  // load dataset into memory
  fp = fopen(dataset_path, "r");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }
 
  print_timestamp(flog);
  fprintf(flog, "Executing with parameters:\n");
  print_timestamp(flog);
  fprintf(flog, "\t -p %d\n", port);
  print_timestamp(flog);
  fprintf(flog, "\t -o %s\n", logfile_path);
  print_timestamp(flog);
  fprintf(flog, "\t -l %d\n", poolsize);
  print_timestamp(flog);
  fprintf(flog, "\t -d %s\n", dataset_path);

  print_timestamp(flog);
  fprintf(flog, "Loading dataset...\n"); 

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  read_csv(fp);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);

  print_timestamp(flog);
  fprintf(flog, "Dataset loaded in ");
  print_time_diff(flog, start, end);
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
  socklen_t addrlen = sizeof(struct sockaddr_in);

  while (true) {
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);

    if (got_sigint == 1) {
      print_timestamp(flog);
      fprintf(flog,"Termination signal received, waiting for ongoing threads to "
             "complete.\n");
      break;
    }

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

  pthread_cond_broadcast(&pool.cond);
  for (int i = 0; i < poolsize; ++i) {
    if (pthread_join(pool.threads[i], NULL) != 0) {
      perror("pthread_join()");
      exit(EXIT_FAILURE);
    }
  }

  print_timestamp(flog);
  fprintf(flog,"All threads have terminated, server shutting down.\n");

  return 0;

}
