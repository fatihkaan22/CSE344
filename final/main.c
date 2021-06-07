#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv_reader.h"

enum sql_command { UNKNOWN = 0, SELECT, SELECT_DISTINCT, UPDATE };

#define SET_IDX(idx, func)                                                     \
  {                                                                            \
    int n;                                                                     \
    if ((n = (func)) == -1)                                                    \
      return -1;                                                               \
    else                                                                       \
      idx += n;                                                                \
  }

#define R "\x1B[31m"
#define C "\x1B[36m"
#define G "\x1B[32m"
#define Y "\x1B[33m"
#define T "\x1B[0m"

typedef struct pair {
  char *key;
  char *value;
} pair;

typedef struct query {
  enum sql_command cmd;
  bool distinct;
  char *columns[128];
  pair column_set[128];
  pair column_filter[128];
} query;

bool query_end(char *s) { return (*s == ';'); }

// check string equality
bool streq(char *s1, char *s2) {
  char *t1 = strdup(s1);
  char *t2 = strdup(s2);
  char *p1 = t1;
  char *p2 = t2;
  for (; *p1; ++p1)
    *p1 = tolower(*p1);
  for (; *p2; ++p2)
    *p2 = tolower(*p2);
  bool res = (strcmp(t1, t2) == 0);
  free(t1);
  free(t2);
  return res;
}

int read_cols(char *tokens[], char *columns[], char *until) {
  int i = 0;
  while (tokens[i] && !streq(tokens[i], until)) {
    columns[i] = tokens[i];
    ++i;
  }
  columns[i] = NULL;
  if (tokens[i] == NULL || i == 0)
    return -1;
  return i;
}

int keyword_match(char *tokens[], char *s1) {
  if (tokens[0] && streq(tokens[0], s1))
    return 1;
  fprintf(stderr, "ERROR: Couldn't parse query, %s was expected.\n", s1);
  return -1;
}

/* int keyword_match2(char *tokens[], char *s1, char *s2) { */
/*   if (tokens[0] && tokens[1] && streq(tokens[0], s1) && streq(tokens[1], s2))
 */
/*     return 2; */
/*   fprintf(stderr, "ERROR: Couldn't parse query, %s %s was expected.\n", s1,
 * s2); */
/*   return -1; */
/* } */

/* int is_from_table(char *tokens[]) { */
/*   return keyword_match2(tokens, "FROM", "TABLE"); */
/* } */

/* int is_table_set(char *tokens[]) { */
/*   return keyword_match2(tokens, "TABLE", "SET"); */
/* } */

// returns number of tokens read
int read_pairs(char *tokens[], pair column_filter[]) {
  int i = 0;
  while (tokens[i] && !(streq(tokens[i], "WHERE") || streq(tokens[i], ";"))) {
    /* printf(">>%s\n", tokens[i]); */
    if (streq(tokens[i], "*")) {
      printf("YILDIZ\n");
      column_filter[i].key = tokens[i];
      return 1;
    }
    // TODO: malloc
    // TODO: think about empty spaces
    char *key = strtok(tokens[i], "=");
    char *val = strtok(NULL, "='");
    /* printf("%s\n", key); */
    /* printf("%s\n", val); */
    column_filter[i].key = key;
    column_filter[i].value = val;
    ++i;
  }
  column_filter[i].key = NULL;
  if (tokens[i] == NULL || i == 0)
    return -1;
  return i;
}

// returns the index after consumed tokens
int which_command(char *tokens[], enum sql_command *command) {
  if (streq(tokens[0], "select")) {
    if (streq(tokens[1], "distinct")) {
      *command = SELECT_DISTINCT;
      return 2;
    } else {
      *command = SELECT;
      return 1;
    }
  }
  if (streq(tokens[0], "update")) {
    *command = UPDATE;
    return 1;
  }
  fprintf(stderr, "Unknown command: %s\n", tokens[0]);
  return -1;
}

void print_query(query q) {
  puts("---QUERY---");
  switch (q.cmd) {
  case SELECT:
    printf("SELECT\n");
    break;
  case UPDATE:
    printf("UPDATE\n");
    break;
  case SELECT_DISTINCT:
    printf("SELECT_DISTINCT\n");
  }
  printf("columns: ");
  for (int i = 0; q.columns[i] != NULL; ++i) {
    printf(" %s ", q.columns[i]);
  }
  printf("\ncolumn_set: ");
  for (int i = 0; q.column_set[i].key != NULL; ++i) {
    printf(" %s=", q.column_set[i].key);
    printf("%s ", q.column_set[i].value);
  }
  printf("\ncolumn_filter: ");
  for (int i = 0; q.column_filter[i].key != NULL; ++i) {
    printf(" %s=", q.column_filter[i].key);
    printf("%s ", q.column_filter[i].value);
  }

  puts("\n-----------");
}

int parse_query(char *query_str, query *q) {
  char delims[] = " \t\r\n ,;";
  char *tokens[64]; // WARN:
  if (query_str[strlen(query_str) - 1] != ';') {
    fprintf(stderr, "ERROR: Query doesn't ends with ';'\n");
    return -1;
  }
  char *token = strtok(query_str, delims);
  int no_tokens = 0;
  // TODO: consider whitespaces before/after =
  while (token) {
    tokens[no_tokens++] = token;
    token = strtok(NULL, delims);
  }
  tokens[no_tokens++] = ";";
  tokens[no_tokens] = NULL;

  int tok_idx = 0;
  SET_IDX(tok_idx, which_command(tokens, &(q->cmd)));

  switch (q->cmd) {
  case SELECT_DISTINCT:
    q->distinct = true;
  case SELECT: {
    SET_IDX(tok_idx, read_cols(tokens + tok_idx, q->columns, "FROM"));
    SET_IDX(tok_idx, keyword_match(tokens + tok_idx, "FROM"));
    SET_IDX(tok_idx, keyword_match(tokens + tok_idx, "TABLE"));
    break;
  }
  case UPDATE: {
    SET_IDX(tok_idx, keyword_match(tokens + tok_idx, "TABLE"));
    SET_IDX(tok_idx, keyword_match(tokens + tok_idx, "SET"));
    SET_IDX(tok_idx, read_pairs(tokens + tok_idx, q->column_set));
    break;
  }
  default:
    puts("ERROR"); // TODO:
  }
  if (query_end(tokens[tok_idx]))
    return 0;

  SET_IDX(tok_idx, keyword_match(tokens + tok_idx, "WHERE"));
  SET_IDX(tok_idx, read_pairs(tokens + tok_idx, q->column_filter));
  SET_IDX(tok_idx, keyword_match(tokens + tok_idx, ";"));
  return 0;
}

int col_idx(char *str) {
  if (!str) {
    fprintf(stderr, "ERROR:  null string\n");
    return -1;
  }

  for (size_t i = 0; i < table.no_cols; ++i) {
    if (strcmp(table.header[i], str) == 0) {
      return i;
    }
  }
  fprintf(stderr, "ERROR: Not in header\n");
  return -1;
}

// checks if given set of entities are actually matches with the header
bool match_header_str(char **cols) {
  for (int i = 0; cols[i] != NULL; ++i) {
    if (col_idx(cols[i]) == -1) {
      fprintf(stderr, "ERROR: Not a valid query, error parsing: %s\n", cols[i]);
      return false;
    }
  }
  return true;
}

// checks if given set of key set are actually matches with the header
bool match_header_pair(pair *cols) {
  for (int i = 0; cols[i].key != NULL; ++i) {
    if (col_idx(cols[i].key) == -1) {
      fprintf(stderr, "ERROR: Not a valid query, error parsing: %s\n",
              cols[i].key);
      return false;
    }
  }
  return true;
}

bool valid_query(query q) {
  if (!match_header_str(q.columns))
    return false;
  if (!match_header_pair(q.column_filter))
    return false;
  if (!match_header_pair(q.column_set))
    return false;
  return true;
  for (int i = 0; q.column_filter[i].key; ++i) {
    if (col_idx(q.column_filter[i].key) == -1) {
      fprintf(stderr, "ERROR: Not a valid query, error parsing: %s\n",
              q.column_filter[i].key);
      return false;
    }
  }
  for (int i = 0; q.column_set[i].key; ++i) {
    if (col_idx(q.column_set[i].key) == -1) {
      fprintf(stderr, "ERROR: Not a valid query, error parsing: %s\n",
              q.column_set[i].key);
      return false;
    }
  }
  return true;
}

bool record_match(int row, pair *column_filter) {
  /* print_row(table.records[row]); */
  for (int i = 0; column_filter[i].key; ++i) {
    int col = col_idx(column_filter[i].key);
    if (col == -1)
      return false;
    if (table.records[row][col] == NULL)
      return false;
    if (strcmp(table.records[row][col], column_filter[i].value) != 0)
      return false;
  }
  return true;
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

void print_table(char **columns, int rows[], int rows_size) {
  if (rows_size == 0)
    return;
  bool include[table.no_cols];
  if (columns[0] == NULL || *columns[0] == '*') {
    memset(&include, 1, sizeof(include));
  } else {
    memset(&include, 0, sizeof(include));
    for (size_t i = 0; columns[i]; ++i) {
      for (size_t j = 0; j < table.no_cols; ++j) {
        if (strcmp(columns[i], table.header[j]) == 0) {
          include[j] = true;
        }
      }
    }
  }

  print_row(table.header, include);
  for (int i = 0; i < rows_size; ++i) {
    print_row(table.records[rows[i]], include);
  }
}

int record_set(int idx, pair* column_set) {
  for (int i = 0; column_set[i].key; ++i) {
    int c = col_idx(column_set[i].key);
    char* deleted = table.records[idx][c];
    table.records[idx][c] = column_set[i].value;
    free(deleted);
  }
  return 0;
}

int run_query(query q) {
  if (!valid_query(q))
    return -1;
  int result_record[table.size + 1]; // TODO:
  size_t res_size = 0;
  switch (q.cmd) {
  case SELECT: {
    for (size_t i = 0; i < table.size; ++i) {
      if (record_match(i, q.column_filter)) {
        result_record[res_size++] = i;
      }
    }
    break;
  }
  case UPDATE:
    for (size_t i = 0; i < table.size; ++i) {
      if (record_match(i, q.column_filter)) {
        result_record[res_size++] = i;
      }
    }
    for (size_t i = 0; i < res_size; ++i) {
      record_set(result_record[i], q.column_set);
    }
    break;
  case SELECT_DISTINCT:
    break;
  default:
    puts("ERROR"); // TODO:
  }

  print_table(q.columns, result_record, res_size);

  return 0;
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
  read_csv(fp);

  // print table
  /* print_row(table.header); */
  /* for (size_t i = 0; i < table.size; ++i) { */
  /*   print_row(table.records[i]); */
  /* } */

  char *cmd;

  cmd = "SELECT Period, Magnitude FROM TABLE WHERE "
        "Series_reference='BDCQ.SEA1AA';";
  /* cmd = "SELECT * FROM TABLE WHERE " */
  /*       "STATUS='F',Series_reference='BDCQ.SEA1AA',Period='2020.12';"; */
  /* cmd = "SELECT columnName1, columnName2, columnName3 FROM TABLE;"; */
  cmd = "UPDATE TABLE SET Period='-1' WHERE Series_reference='BDCQ.SEA1AA';";
  /* cmd = "UPDATE TABLE SET columnName1='value1';"; */
  /* cmd = "SELECT DISTINCT columnName1,columnName2 FROM TABLE;"; */

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
