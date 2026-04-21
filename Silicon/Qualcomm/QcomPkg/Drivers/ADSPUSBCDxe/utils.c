#include <utils.h>

extern void bitset256_clear(bitset256_t*restrict set);
extern void bitset256_set(bitset256_t*restrict set, UINT8 index);
extern void bitset256_unset(bitset256_t*restrict set, UINT8 index);
extern BOOLEAN bitset256_get(bitset256_t*restrict set, UINT8 index);
static inline BOOLEAN bitset256_is_empty(bitset256_t*restrict set);

extern void bitset128_clear(bitset128_t*restrict set);
extern void bitset128_set(bitset128_t*restrict set, int index);
extern void bitset128_unset(bitset128_t*restrict set, int index);
extern BOOLEAN bitset128_get(bitset128_t*restrict set, int index);
static inline BOOLEAN bitset128_is_empty(bitset128_t*restrict set);

extern void bitset64_clear(bitset64_t*restrict set);
extern void bitset64_set(bitset64_t*restrict set, int index);
extern void bitset64_unset(bitset64_t*restrict set, int index);
extern BOOLEAN bitset64_get(bitset64_t*restrict set, int index);
static inline BOOLEAN bitset64_is_empty(bitset64_t*restrict set);

void hexdump(const void* vdata, unsigned size){
  const UINT8* data = vdata;
  static const char digits[] = "0123456789ABCDEF ";
  for(unsigned i=0; i<size; i+=16){
    char line[] = "                                                  |                  ";
    for(unsigned j=0; j<16 && i+j<size; j++){
      UINT8 ch = data[i+j];
      int off = j*3 + (j>=8);
      line[off+1] = digits[ch/16];
      line[off+2] = digits[ch%16];
      if(ch < 0x7F && ch >= 0x20){
        line[52+j + (j>=8)] = ch;
      }else{
        line[52+j + (j>=8)] = '.';
      }
    }
    DEBUG((EFI_D_WARN, " %a\n", line));
  }
}
