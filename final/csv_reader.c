#include "csv_reader.h"
#include <stdlib.h>
#include <string.h>

struct sql_table table;

void read_csv(FILE *fp) {

  char buf[LINE];

  table.header = (record)malloc(MAX_ROW * sizeof(char *));

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
  table.records = (record *)malloc(MAX_COL * sizeof(record));
  while (fgets(buf, sizeof(buf), fp)) {
    buf[strcspn(buf, "\n")] = '\0'; // remove trailing \n
    buf[strcspn(buf, "\r")] = '\n'; // remove trailing \r
    int local_cols = 0;
    record r;
    r = (record)malloc(MAX_ROW * sizeof(char *));
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
      fprintf(stderr,
              "ERROR: Number of columns in line %ld is different than in the "
              "header's\n",
              table.size + 1); // size + 1 is the current line
    }
    table.size++;
  }
  fclose(fp);
}

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

void print_row(record r, bool include[]) {
  bool nl = false;
  for (size_t i = 0; i < table.no_cols; ++i) {
    if (include == NULL || include[i]) {
      printf("%s | ", r[i]);
      nl = true;
    }
  }
  if (nl)
    puts("");
}

int sprint_row(char *buffer, record r, bool include[]) {
  int len = 0;
  bool nl = false;
  for (size_t i = 0; i < table.no_cols; ++i) {
    if (include == NULL || include[i]) {
      len += sprintf(buffer + len, "%s | ", r[i]);
      nl = true;
    }
  }
  if (nl)
    len += sprintf(buffer + len, "\n");
  return len;
}

void set_include_cols(bool *include, char **columns) {
  if (columns[0] == NULL || *columns[0] == '*') {
    /* memset(&include, 1, sizeof(include)); */
    memset(include, 1, table.no_cols * sizeof(bool));
  } else {
    /* memset(&include, 0, sizeof(include)); */
    memset(include, 0, table.no_cols * sizeof(bool));
    for (size_t i = 0; columns[i]; ++i) {
      for (size_t j = 0; j < table.no_cols; ++j) {
        if (strcmp(columns[i], table.header[j]) == 0) {
          include[j] = true;
        }
      }
    }
  }
}

void print_table(char **columns, int rows[], int rows_size) {
  if (rows_size == 0)
    return;

  bool include[table.no_cols];
  set_include_cols(include, columns);

  print_row(table.header, include);
  for (int i = 0; i < rows_size; ++i) {
    print_row(table.records[rows[i]], include);
  }
}
