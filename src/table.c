#include <stdint.h>
#include <string.h>

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
  Entry *tombstone = NULL;
  while (true) {
    Entry *entry = &entries[idx];
    if (entry->key == NULL) {
      if (IS_NULL(entry->value)) {
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) {
          tombstone = entry;
        }
      }
    } else if (entry->key == key) {
      return entry;
    }

    idx = (idx + 1) % capacity;
  }
}

bool table_get(const Table *table, const ObjString *key, Value *value) {
  if (table->len == 0) {
    return false;
  }

  Entry *entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  *value = entry->value;
  return true;
}

static void adjust_capacity(Table *table, uint32_t capacity) {
  Entry *entries = ALLOCATE(Entry, capacity);
  for (uint32_t i = 0; i < capacity; i += 1) {
    entries[i].key = NULL;
    entries[i].value = NULL_VAL;
  }

  table->len = 0;
  for (uint32_t i = 0; i < table->capacity; i += 1) {
    Entry *entry = &table->entries[i];
    if (entry->key == NULL) {
      continue;
    }
    Entry *dest = find_entry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->len += 1;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool table_insert(Table *table, ObjString *key, Value value) {
  if (table->len + 1 > table->capacity * TABLE_MAX_LOAD) {
    uint32_t capacity = GROW_CAPACITY(table->capacity);
    adjust_capacity(table, capacity);
  }
  Entry *entry = find_entry(table->entries, table->capacity, key);
  bool is_new_key = entry->key == NULL;
  if (is_new_key) {
    entry->key = key;
    if (IS_NULL(entry->value)) {
      table->len += 1;
    }
  }

  entry->value = value;
  return is_new_key;
}

bool table_remove(Table *table, const ObjString *key) {
  if (table->len == 0) {
    return false;
  }

  Entry *entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  entry->key = NULL;
  entry->value = BOOL_VAL(true);
  return true;
}

void table_copy(const Table *src, Table *dest) {
  for (uint32_t i = 0; i < src->capacity; i += 1) {
    Entry *entry = &src->entries[i];
    if (entry->key != NULL) {
      table_insert(dest, entry->key, entry->value);
    }
  }
}

ObjString *table_find_string(Table *table, const char *chars, uint32_t len,
                             uint32_t hash) {
  if (table->len == 0) {
    return NULL;
  }

  uint32_t idx = hash % table->capacity;
  while (true) {
    Entry *entry = &table->entries[idx];
    if (entry->key == NULL) {

      if (IS_NULL(entry->value)) {
        return NULL;
      }
    } else if (entry->key->len == len && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, len) == 0) {
      return entry->key;
    }

    idx = (idx + 1) % table->capacity;
  }
}

void table_remove_white(Table *table) {
  for (uint32_t idx = 0; idx < table->len; idx += 1) {
    Entry *entry = &table->entries[idx];
    if (entry->key != NULL && !entry->key->obj.is_marked) {
      table_remove(table, entry->key);
    }
  }
}

void mark_table(Table *table) {
  for (uint32_t i = 0; i < table->capacity; i += 1) {
    Entry *entry = &table->entries[i];
    mark_object((Obj *)entry->key);
    mark_value(entry->value);
  }
}
