#ifndef UTILS_H
#define UTILS_H

typedef UINT64 bitset64_t;
typedef struct bitset128 bitset128_t;
typedef struct bitset256 bitset256_t;

struct bitset128 {
  UINT64 value[2];
};

struct bitset256 {
  UINT64 value[4];
};


static inline void bitset256_clear(bitset256_t*restrict set){
  *set = (bitset256_t){0};
}

static inline void bitset256_set(bitset256_t*restrict set, UINT8 index){
  set->value[index/64] |= 1<<(index % 64);
}

static inline BOOLEAN bitset256_get(bitset256_t*restrict set, UINT8 index){
  return !!(set->value[index/64] & (1<<(index % 64)));
}

static inline BOOLEAN bitset256_is_empty(bitset256_t*restrict set){
  return !(set->value[0] || set->value[1] || set->value[2] || set->value[3]);
}


static inline void bitset128_clear(bitset128_t*restrict set){
  *set = (bitset128_t){0};
}

static inline void bitset128_set(bitset128_t*restrict set, int index){
  ASSERT(index < 0x80);
  set->value[index/64] |= 1<<(index % 64);
}

static inline BOOLEAN bitset128_get(bitset128_t*restrict set, int index){
  if(index >= 0x80)
    return FALSE;
  return !!(set->value[index/64] & (1<<(index % 64)));
}

static inline BOOLEAN bitset128_is_empty(bitset128_t*restrict set){
  return !(set->value[0] || set->value[1]);
}


static inline void bitset64_clear(bitset64_t*restrict set){
  *set = 0;
}

static inline void bitset64_set(bitset64_t*restrict set, int index){
  ASSERT(index < 0x40);
  *set |= 1<<index;
}

static inline BOOLEAN bitset64_get(bitset64_t*restrict set, int index){
  if(index >= 0x40)
    return FALSE;
  return !!(*set & (1<<index));
}

static inline BOOLEAN bitset64_is_empty(bitset64_t*restrict set){
  return !*set;
}


#endif
