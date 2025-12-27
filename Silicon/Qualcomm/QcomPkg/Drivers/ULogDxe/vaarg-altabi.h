#ifndef VAARG_ALTABI_H
#define VAARG_ALTABI_H

typedef struct A2_VA_LIST_s {
  char* arg_ptr;
  char* stack_base;
  void* x;
  int offset;
  // 4 bytes padding, probably.
} *A2_VA_LIST;

static inline void* a2_va_arg(A2_VA_LIST ap, unsigned n){
  n = (n + 7) & ~7;
  void* ret = 0;
  if(ap->offset <= -n){
    ret = ap->stack_base + ap->offset;
    ap->offset += n;
  }else{
    ret = ap->arg_ptr;
    ap->arg_ptr += n;
  }
  return ret;
}

#define A2_VA_ARG(ap, T) *(T*)a2_va_arg(ap, sizeof(T))

#endif
