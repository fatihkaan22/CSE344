#ifndef CSV_READER_H
#define CSV_READER_H

#include <stdbool.h>
#include <stdio.h>

#define DELIM ","

typedef char **record;

struct sql_table {
  size_t size;
  record header;
  record *records;
  size_t no_cols;
};

extern struct sql_table table;

void read_csv(FILE *fp);
char *csv_tokenizer(char *str, char **rest);
void print_row(record r, bool include[]);
void print_table(char **columns, int rows[], int rows_size);

#endif /* end of include guard: CSV_READER_H */
