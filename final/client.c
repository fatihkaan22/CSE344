#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket_io.h"

#define BUF_SIZE 1024

void usage() {
  printf(
      "Usage: ./client -i id -a 127.0.0.1 -p port -o path/to/queryfile\n"
      " -i id         integer id of the client >= 1\n"
      " -a address    IPv4 address of the machine running the server\n"
      " -p port       port number at which the server waits for connections\n"
      " -o logfile    path of the file containing an arbitrary number of "
      "queries\n");
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

int main(int argc, char *argv[]) {
  int opt;
  uint16_t port = 0;
  unsigned int id = 0;
  char *queryfile_path = NULL;
  char *ip = NULL;

  while ((opt = getopt(argc, argv, "i:a:p:o:")) != -1) {
    switch (opt) {
    case 'i':
      id = try_parse_int(optarg);
      if (id < 1) {
        fprintf(stderr,
                "ERROR: poolsize need to be larger or equal to 2, got: %d\n",
                id);
        usage();
      }
      break;
    case 'a':
      ip = optarg;
      break;
    case 'p':
      port = try_parse_int(optarg);
      break;
    case 'o':
      queryfile_path = optarg;
      break;
    default:
      usage();
    }
  }

  FILE *fp = fopen(queryfile_path, "r");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }

  int sock;
  struct sockaddr_in serv_addr;
  char query[BUF_SIZE];
  char respond[BUF_SIZE];
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket()");
    exit(EXIT_FAILURE);
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  serv_addr.sin_addr.s_addr = inet_addr(ip);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
    perror("connect()");
    exit(EXIT_FAILURE);
  }

  int i = 0;
  while (fgets(query, sizeof(respond), fp)) {
    query[strcspn(query, "\n")] = 0; // remove trailing \n
    send_line(query, strlen(query) + 1, sock);
    // first read gets the how many records are there
    int no_records;
    receive_int(&no_records, sock);
    printf("%d\n", no_records);
    for (int j = 0; j < no_records + 1; ++j) { // number of records + header
      if(receive_line(respond, sock) == -1) {
        fprintf(stderr, "ERROR: couldn't receive_line\n");
      }
      printf("%s\n", respond);
    }
    ++i;
  }
  return 0;
}
