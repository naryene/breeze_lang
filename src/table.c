

#include "table.h"
#include "memory.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void init_table(Table *table) {
  table->len = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void free_table(Table *table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  init_table(table);
}

static Entry *find_entry(Entry *entries, uint32_t capacity,
                         const ObjString *key) {
  uint32_t idx = key->hash % capacity;
  while (true) {
    Entry *entry = &entries[idx];
    if (entry->key == key || entry->key == NULL) {
      return entry;
    }

    idx = (idx + 1) % capacity;
  }
}

static void adjust_capacity(Table *table, uint32_t capacity) {
  Entry *entries = ALLOCATE(Entry, capacity);
  for (uint32_t i=0; i<capacity; i+=1) {
    entries[i].key = NULL;
    entries[i].value = NULL_VAL;
  }

  for (uint32_t i=0; i<table->capacity; i+=1) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) {
      continue;
    }
    Entry *dest = find_entry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool table_insert(Table *table, const ObjString *key, Value value) {
  if (table->len + 1 > table->capacity * TABLE_MAX_LOAD) {
    uint32_t capacity = GROW_CAPACITY(table->capacity);
    adjust_capacity(table, capacity);
  }
  Entry *entry = find_entry(table->entries, table->capacity, key);
  bool is_new_key = entry->key == NULL;
  if (is_new_key) {
    table->len += 1;
    entry->key = key;
  }

  entry->value = value;
  return is_new_key;
}

void table_copy(Table *src, Table *dest) {
  for (uint32_t i=0; i<src->capacity; i+=1) {
    Entry *entry = &src->entries[i];
    if (entry->key != NULL) {
      table_insert(dest, entry->key, entry->value);
    }
  }
}
