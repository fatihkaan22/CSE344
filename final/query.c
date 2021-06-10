#include "query.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SET_IDX(idx, func)                                                     \
  {                                                                            \
    int n;                                                                     \
    if ((n = (func)) == -1)                                                    \
      return -1;                                                               \
    else                                                                       \
      idx += n;                                                                \
  }

int run_query(query q, int result_records[], unsigned int *res_size) {
  if (!valid_query(q))
    return -1;
  *res_size = 0;
  switch (q.cmd) {
  case SELECT: {
    for (size_t i = 0; i < table.size; ++i) {
      if (record_match(i, q.column_filter)) {
        result_records[(*res_size)++] = i;
      }
    }
    break;
  }
  case UPDATE:
    for (size_t i = 0; i < table.size; ++i) {
      if (record_match(i, q.column_filter)) {
        result_records[(*res_size)++] = i;
      }
    }
    for (size_t i = 0; i < *res_size; ++i) {
      record_set(result_records[i], q.column_set);
    }
    break;
  case SELECT_DISTINCT:
    for (size_t i = 0; i < table.size; ++i) {
      if (record_match(i, q.column_filter) &&
          !contains_all(result_records, *res_size, q.columns,
                        table.records[i])) {
        result_records[(*res_size)++] = i;
      }
    }
    break;
  default:
    fprintf(stderr, "ERROR: run query, default\n");
    return -1;
  }

  /* print_table(q.columns, result_records, *res_size); */

  return 0;
}

int parse_query(char *query_str, query *q) {
  char delims[] = " \t\r\n ,;";
  char *tokens[128];
  if (query_str[strlen(query_str) - 1] != ';') {
    fprintf(stderr, "ERROR: Semicolon expected at the end of the query\n");
    return -1;
  }
  char *token = strtok(query_str, delims);
  int no_tokens = 0;
  // TODO: consider whitespaces before/after =
  memset(q, 0, sizeof(query));
  while (token) {
    tokens[no_tokens++] = token;
    token = strtok(NULL, delims);
  }
  tokens[no_tokens++] = ";";
  tokens[no_tokens] = NULL;

  q->distinct = false;

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
    fprintf(stderr, "ERROR: parse query, default\n");
  }
  if (query_end(tokens[tok_idx]))
    return 0;

  SET_IDX(tok_idx, keyword_match(tokens + tok_idx, "WHERE"));
  SET_IDX(tok_idx, read_pairs(tokens + tok_idx, q->column_filter));
  SET_IDX(tok_idx, keyword_match(tokens + tok_idx, ";"));
  return 0;
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
    break;
  default:
    fprintf(stderr, "ERROR: run query, default\n");
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

int keyword_match(char *tokens[], char *s1) {
  if (tokens[0] && streq(tokens[0], s1))
    return 1;
  fprintf(stderr, "ERROR: Couldn't parse query, %s was expected.\n", s1);
  return -1;
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

// returns number of tokens read
int read_pairs(char *tokens[], pair column_filter[]) {
  int i = 0;
  while (tokens[i] && !(streq(tokens[i], "WHERE") || streq(tokens[i], ";"))) {
    if (streq(tokens[i], "*")) {
      column_filter[i].key = tokens[i];
      return 1;
    }
    // TODO: think about empty spaces
    char *key = strtok(tokens[i], "=");
    char *val = strtok(NULL, "='");
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
  if (*q.columns && *q.columns[0] != '*' && !match_header_str(q.columns))
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

int record_set(int idx, pair *column_set) {
  for (int i = 0; column_set[i].key; ++i) {
    int c = col_idx(column_set[i].key);
    char *delete = table.records[idx][c];
    table.records[idx][c] = malloc(strlen(column_set[i].value) + 1);
    strcpy(table.records[idx][c], column_set[i].value);
    free(delete);
  }
  return 0;
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

bool contains_all(int *arr, int arr_size, char **filter_cols, record r) {
  for (int i = 0; filter_cols[i]; ++i)
    if (!contains(arr, arr_size, filter_cols[i], r))
      return false;
  return true;
}

bool contains(int *arr, int arr_size, char *filter, record r) {
  int col = col_idx(filter);
  for (int i = 0; i < arr_size; ++i) {
    int row = arr[i];
    // TODO: consider adding null to the list
    if (r[col] == NULL && table.records[row][col] == NULL)
      return true;
    if (r[col] == NULL || table.records[row][col] == NULL)
      continue;
    if (strcmp(table.records[row][col], r[col]) == 0)
      return true;
  }
  return false;
}

bool query_select(enum sql_command cmd) {
  return (cmd == SELECT || cmd == SELECT_DISTINCT);
}

bool query_update(enum sql_command cmd) { return (cmd == UPDATE); }
