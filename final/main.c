#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum sql_commands { SELECT, SELECT_DISTINCT, UPDATE };
enum sql_clause { DISTINCT, WHERE };

#define DELIM ","

#define R "\x1B[31m"
#define C "\x1B[36m"
#define G "\x1B[32m"
#define Y "\x1B[33m"
#define T "\x1B[0m"

typedef char **record;

struct sql_table {
  size_t size;
  record header;
  record *records;
  size_t no_cols;
};

struct sql_table table;

char *csv_tokenizer(char *str, char **rest) {
  if (str == NULL) {
    str = *rest;
  }
  if (*str == '\n') {
    *str = '\0';
    return str;
  }
  if (*str == '\0') {
    *rest = str;
    return NULL;
  }
  int next_delim = strcspn(str, DELIM);
  int next_close_dquote;
  if (*str == '\"') { // to escape DELIMs in double quotes
    next_close_dquote = strcspn(str + 1, "\"");
    if (next_close_dquote > next_delim) {
      next_close_dquote++;
      next_delim = next_close_dquote + strcspn(str + next_close_dquote, DELIM);
    }
  }
  char *end = str + next_delim;
  if (*end == '\0') {
    if (*(end - 1) == '\n')
      *(end - 1) = 0;
    *rest = end;
    return str;
  }
  *end = '\0';
  *rest = end + 1;
  return str;
}

void print_row(record r) {
  for (size_t i = 0; i < table.no_cols; ++i) {
    printf("%s | ", r[i]);
  }
  puts("");
}

void read_csv(FILE *fp) {

  char buf[1024]; // TODO: read in parts

  table.header = (record)malloc(16 * sizeof(char *)); // TODO:

  if (fgets(buf, sizeof(buf), fp) == NULL) {
    fprintf(stderr, "fgets error\n");
  }
  buf[strcspn(buf, "\n")] = 0;    // remove trailing \n
  buf[strcspn(buf, "\r")] = '\n'; // remove trailing \r
  char *token;
  char *rest = buf;
  table.no_cols = 0;
  while ((token = csv_tokenizer(rest, &rest))) {
    table.header[table.no_cols] = malloc(strlen(token) + 1);
    strcpy(table.header[table.no_cols], token);
    ++table.no_cols;
  }

  table.size = 0;
  table.records = (record *)malloc(256 * sizeof(record));
  while (fgets(buf, sizeof(buf), fp)) {
    buf[strcspn(buf, "\n")] = '\0'; // remove trailing \n
    buf[strcspn(buf, "\r")] = '\n'; // remove trailing \r
    int local_cols = 0;
    record r;
    r = (record)malloc(16 * sizeof(char *)); // TODO:
    rest = buf;
    while ((token = csv_tokenizer(rest, &rest))) {
      if (*token == 0) {
        r[local_cols] = NULL;
      } else {
        r[local_cols] = malloc(strlen(token) + 1);
        strcpy(r[local_cols], token);
      }
      ++local_cols;
    }
    table.records[table.size] = r;
    /* printf("%d %d\n", local_cols, table.no_cols); */
    if (local_cols != table.no_cols) {
      fprintf(stdout, // FIXME: stderr
              "ERROR: Number of columns in line %ld is different than in the "
              "header's\n",
              table.size + 1); // size + 1 is the current line
    }
    table.size++;
  }

}

int main(int argc, char *argv[]) {
  printf(R "-------------------------------------------------------------------"
           "-------------------------------------------------------------------"
           "-------------\n" T);

  char *filepath = "machine.csv";
  FILE *fp = fopen(filepath, "r"); // TODO: check
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file");
    exit(EXIT_FAILURE);
  }
  /* read_csv(fp); */

  /* // print table */
  /* print_row(table.header); */
  /* for (size_t i = 0; i < table.size; ++i) { */
  /*   print_row(table.records[i]); */
  /* } */




  return 0;
}
