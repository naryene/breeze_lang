#ifndef breeze_table_h
#define breeze_table_h

#include <stdint.h>
#include <stdbool.h>

#include "common.h"
#include "value.h"

typedef struct {
  ObjString *key;
  Value value;
} Entry;

typedef struct {
  uint32_t len;
  uint32_t capacity;
  Entry *entries;
} Table;

void init_table(Table *table);
void free_table(Table *table);
bool table_get(const Table *table, const ObjString *key, Value *value);
bool table_insert(Table *table, ObjString *key, Value value);
bool table_remove(Table *table, const ObjString *key);
void table_copy(const Table *src, Table *dest);
ObjString *table_find_string(Table *table, const char *chars,
                             uint32_t len, uint32_t hash);
void table_remove_white(Table *table);
void mark_table(Table *table);


#endif // !breeze_table_h
