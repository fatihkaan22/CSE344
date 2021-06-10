#ifndef QUERY_H
#define QUERY_H

#include "csv_reader.h"
#include <stdbool.h>

enum sql_command { UNKNOWN = 0, SELECT, SELECT_DISTINCT, UPDATE };

#define MAX_F 128

typedef struct pair {
  char *key;
  char *value;
} pair;

typedef struct query {
  enum sql_command cmd;
  bool distinct;
  char *columns[MAX_F];
  pair column_set[MAX_F];
  pair column_filter[MAX_F];
} query;


int parse_query(char *query_str, query *q);
int run_query(query q, int result_records[], unsigned int *res_size);
bool query_select(enum sql_command cmd);
bool query_update(enum sql_command cmd);

// helpers
int keyword_match(char *tokens[], char *s1);
int read_cols(char *tokens[], char *columns[], char *until);
int read_pairs(char *tokens[], pair column_filter[]);
int which_command(char *tokens[], enum sql_command *command);
bool query_end(char *s);
bool streq(char *s1, char *s2);
int col_idx(char *str);
bool match_header_str(char **cols);
bool match_header_pair(pair *cols);
bool valid_query(query q);
int record_set(int idx, pair* column_set) ;
bool record_match(int row, pair *column_filter) ;
bool contains(int *arr, int arr_size, char *filter, record r) ;
bool contains_all (int *arr, int arr_size, char **filter_cols, record r);

// debug functions
void print_query(query q);

#endif /* end of include guard: QUERY_H */
