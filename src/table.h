#ifndef breeze_table_h
#define breeze_table_h

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "value.h"

typedef struct TableEntry {
  ObjString *key;
  Value value;
} TableEntry;

typedef struct SetEntry {
  ObjString *key;
  bool is_tombstone;
} SetEntry;

typedef struct Table {
  uint32_t len;
  uint32_t capacity;
  TableEntry *entries;
} Table;

typedef struct Set {
  uint32_t len;
  uint32_t capacity;
  SetEntry *entries;
} Set;

void init_table(Table *table);
void free_table(Table *table);
bool table_contains(const Table *table, const ObjString *key);
bool table_get(const Table *table, const ObjString *key, Value *value);
bool table_insert(Table *table, ObjString *key, Value value);
bool table_remove(Table *table, const ObjString *key);
void table_copy(const Table *src, Table *dst);
ObjString *table_find_string(const Table *table, const char *chars,
                             uint32_t len, uint32_t hash);
void table_remove_white(Table *table);
void mark_table(Table *table);

void init_set(Set *set);
void free_set(Set *set);
bool set_contains(const Set *set, const ObjString *key);
bool set_insert(Set *set, ObjString *key);
bool set_remove(Set *set, const ObjString *key);
void set_copy(const Set *src, Set *dst);
ObjString *set_find_string(const Set *set, const char *chars, uint32_t len,
                           uint32_t hash);
void set_remove_white(Set *set);
void mark_set(Set *set);

#endif // !breeze_table_h
