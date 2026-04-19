#ifndef UTILS_H
#define UTILS_H

struct bitset256 {
  UINT64 value[4];
};

static inline void bitset256_clear(struct bitset256*restrict set){
  *set = (struct bitset256){0};
}

static inline void bitset256_set(struct bitset256*restrict set, UINT8 value){
  set->value[value/64] |= 1<<(value % 64);
}

static inline BOOLEAN bitset256_get(struct bitset256*restrict set, UINT8 value){
  return !!(set->value[value/64] & (1<<(value % 64)));
}

#endif
