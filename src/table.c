#include <assert.h>
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
  FREE_ARRAY(TableEntry, table->entries, table->capacity);
  init_table(table);
}

static TableEntry *find_table_entry(TableEntry *entries, uint32_t capacity,
                                    const ObjString *key) {
  // Capacities are powers of two, so masking equals `% capacity` without a
  // hardware divide.
  uint32_t mask = capacity - 1;
  uint32_t idx = key->hash & mask;
  TableEntry *tombstone = NULL;
  while (true) {
    TableEntry *entry = &entries[idx];
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

    idx = (idx + 1) & mask;
  }
}

bool table_contains(const Table *table, const ObjString *key) {
  if (table->len == 0) {
    return false;
  }
  TableEntry *entry = find_table_entry(table->entries, table->capacity, key);
  return entry->key != NULL;
}



bool table_get(const Table *table, const ObjString *key, Value *value) {
  if (table->len == 0) {
    return false;
  }

  TableEntry *entry = find_table_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  *value = entry->value;
  return true;
}

static void adjust_table_capacity(Table *table, uint32_t capacity) {
  assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of two");
  TableEntry *entries = ALLOCATE(TableEntry, capacity);
  for (uint32_t i = 0; i < capacity; i += 1) {
    entries[i].key = NULL;
    entries[i].value = NULL_VAL;
  }

  table->len = 0;
  for (uint32_t i = 0; i < table->capacity; i += 1) {
    TableEntry *entry = &table->entries[i];
    if (entry->key == NULL) {
      continue;
    }
    TableEntry *dst = find_table_entry(entries, capacity, entry->key);
    dst->key = entry->key;
    dst->value = entry->value;
    table->len += 1;
  }

  FREE_ARRAY(TableEntry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool table_insert(Table *table, ObjString *key, Value value) {
  if (table->len + 1 > table->capacity * TABLE_MAX_LOAD) {
    uint32_t capacity = GROW_CAPACITY(table->capacity);
    adjust_table_capacity(table, capacity);
  }
  TableEntry *entry = find_table_entry(table->entries, table->capacity, key);
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

  TableEntry *entry = find_table_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  entry->key = NULL;
  // setting the entry as a tombstone
  entry->value = BOOL_VAL(true);
  return true;
}

void table_copy(const Table *src, Table *dst) {
  for (uint32_t i = 0; i < src->capacity; i += 1) {
    TableEntry *entry = &src->entries[i];
    if (entry->key != NULL) {
      table_insert(dst, entry->key, entry->value);
    }
  }
}

ObjString *table_find_string(const Table *table, const char *chars,
                             uint32_t len, uint32_t hash) {
  if (table->len == 0) {
    return NULL;
  }

  uint32_t mask = table->capacity - 1;
  uint32_t idx = hash & mask;
  while (true) {
    TableEntry *entry = &table->entries[idx];
    if (entry->key == NULL) {
      if (IS_NULL(entry->value)) {
        return NULL;
      }
    } else if (entry->key->len == len && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, len) == 0) {
      return entry->key;
    }

    idx = (idx + 1) & mask;
  }
}

void table_remove_white(Table *table) {
  // `len` counts live entries plus tombstones, not slots: scan every slot.
  for (uint32_t idx = 0; idx < table->capacity; idx += 1) {
    TableEntry *entry = &table->entries[idx];
    if (entry->key != NULL && !entry->key->obj.is_marked) {
      table_remove(table, entry->key);
    }
  }
}

void mark_table(Table *table) {
  for (uint32_t i = 0; i < table->capacity; i += 1) {
    TableEntry *entry = &table->entries[i];
    mark_object((Obj *)entry->key);
    mark_value(entry->value);
  }
}

void init_set(Set *set) {
  set->len = 0;
  set->capacity = 0;
  set->entries = NULL;
}

void free_set(Set *set) {
  FREE_ARRAY(SetEntry, set->entries, set->capacity);
  init_set(set);
}

static SetEntry *find_set_entry(SetEntry *entries, uint32_t capacity,
                                const ObjString *key) {
  // Capacities are powers of two, so masking equals `% capacity` without a
  // hardware divide.
  uint32_t mask = capacity - 1;
  uint32_t idx = key->hash & mask;
  SetEntry *tombstone = NULL;

  while (true) {
    SetEntry *entry = &entries[idx];
    if (entry->key == NULL) {
      if (!entry->is_tombstone) {
        return tombstone != NULL ? tombstone : entry;
      } else if (tombstone == NULL) {
        tombstone = entry;
      }
    } else if (entry->key == key) {
      return entry;
    }

    idx = (idx + 1) & mask;
  }
}

bool set_contains(const Set *set, const ObjString *key) {
  if (set->len == 0) {
    return false;
  }
  SetEntry *entry = find_set_entry(set->entries, set->capacity, key);
  return entry->key != NULL;
}

static void adjust_set_capacity(Set *set, uint32_t capacity) {
  assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of two");
  SetEntry *entries = ALLOCATE(SetEntry, capacity);
  for (uint32_t i = 0; i < capacity; i += 1) {
    entries[i].key = NULL;
    entries[i].is_tombstone = false;
  }

  set->len = 0;
  for (uint32_t i = 0; i < set->capacity; i += 1) {
    SetEntry *entry = &set->entries[i];
    if (entry->key == NULL) {
      continue;
    }
    SetEntry *dst = find_set_entry(entries, capacity, entry->key);
    dst->key = entry->key;
    dst->is_tombstone = entry->is_tombstone;
    set->len += 1;
  }

  FREE_ARRAY(SetEntry, set->entries, set->capacity);
  set->entries = entries;
  set->capacity = capacity;
}

bool set_insert(Set *set, ObjString *key) {
  if (set->len + 1 > set->capacity * TABLE_MAX_LOAD) {
    uint32_t capacity = GROW_CAPACITY(set->capacity);
    adjust_set_capacity(set, capacity);
  }
  SetEntry *entry = find_set_entry(set->entries, set->capacity, key);
  bool is_new = (entry->key == NULL);
  if (is_new) {
    entry->key = key;
    if (!entry->is_tombstone) {
      set->len += 1;
    }
    entry->is_tombstone = false;
  }
  return is_new;
}

bool set_remove(Set *set, const ObjString *key) {
  if (set == NULL || set->len == 0) {
    return false;
  }

  SetEntry *entry = find_set_entry(set->entries, set->capacity, key);

  if (entry->key == NULL) {
    return false;
  }

  entry->key = NULL;
  entry->is_tombstone = true;
  return true;
}

void set_remove_white(Set *set) {
  for (uint32_t idx = 0; idx < set->capacity; idx += 1) {
    SetEntry *entry = &set->entries[idx];
    if (entry->key != NULL && !entry->key->obj.is_marked) {
      set_remove(set, entry->key);
    }
  }
}

void mark_set(Set *set) {
  for (uint32_t i = 0; i < set->capacity; i += 1) {
    SetEntry *entry = &set->entries[i];
    mark_object((Obj *)entry->key);
  }
}
