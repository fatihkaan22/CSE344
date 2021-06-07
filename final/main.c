#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv_reader.h"
#include "query.h"

#define R "\x1B[31m"
#define C "\x1B[36m"
#define G "\x1B[32m"
#define Y "\x1B[33m"
#define T "\x1B[0m"

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
  read_csv(fp);

  // print table
  /* print_row(table.header); */
  /* for (size_t i = 0; i < table.size; ++i) { */
  /*   print_row(table.records[i]); */
  /* } */

  char *cmd;

  /* cmd = "SELECT Period, Magnitude FROM TABLE WHERE " */
  /*       "Series_reference='BDCQ.SEA1AA';"; */
  /* cmd = "SELECT * FROM TABLE WHERE " */
  /*       "STATUS='F',Series_reference='BDCQ.SEA1AA',Period='2020.12';"; */
  /* cmd = "SELECT columnName1, columnName2, columnName3 FROM TABLE;"; */
  /* cmd = "UPDATE TABLE SET Period='-1' WHERE Series_reference='BDCQ.SEA1AA';"; */
  /* cmd = "UPDATE TABLE SET columnName1='value1';"; */
  cmd = "SELECT DISTINCT Series_reference,Series_title_5 FROM TABLE;";

  char *query_str = malloc(256);
  strcpy(query_str, cmd);

  query q;
  memset(&q, 0, sizeof(q));
  if (parse_query(query_str, &q) == -1) {
    fprintf(stderr, "Error while parsing query: '%s'\n", cmd);
  }

  print_query(q);

  if (run_query(q) == -1) {
    fprintf(stderr, "Error while running query: '%s'\n", cmd);
  }

  return 0;
}
