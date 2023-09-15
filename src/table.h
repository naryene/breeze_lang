#ifndef breeze_table_h
#define breeze_table_h

#include "common.h"
#include "value.h"

typedef struct {
  const ObjString *key;
  Value value;
} Entry;

typedef struct {
  uint32_t len;
  uint32_t capacity;
  Entry *entries;
} Table;

void init_table(Table *table);
void free_table(Table *table);
bool table_insert(Table *table, const ObjString *key, Value value);
void table_copy(Table *src, Table *dest);

#endif // !breeze_table_h
