#ifndef CSV_READER_H
#define CSV_READER_H

#include <stdio.h>
#include <stdbool.h>

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

#endif /* end of include guard: CSV_READER_H */
