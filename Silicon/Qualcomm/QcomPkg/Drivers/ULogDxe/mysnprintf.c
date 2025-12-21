#include <Library/PrintLib.h>
#include "mysnprintf.h"

enum int_format_e {
  F_SIGNED,
  F_UNSIGNED,
  F_octal,
  F_hex,
  F_HEX,
};

struct print_option {
  enum int_format_e int_format;
  unsigned long width;
  unsigned long precision;
  unsigned left_justify : 1;
  unsigned force_sign : 1;
  unsigned sign_space_blank : 1;
  unsigned prefix : 1;
  unsigned pad_zero : 1;
  unsigned is_signed : 1;
};

static unsigned long print_string_formatted(char*restrict out, unsigned long size, const char*restrict val, const struct print_option*restrict opts){
  if(opts->precision && size > opts->precision)
    size = opts->precision;
  if(!val)
    val = "(null)";
  unsigned long i = 0;
  for(char c; i < size && (c=val[i]); i++){
    out[i] = c;
  }
  const unsigned long width = opts->width < size ? opts->width : size;
  if(width > i){
    const char filler = opts->pad_zero ? '0' : ' ';
    if(opts->left_justify){
      while(i < width)
        out[i++] = filler;
    }else{
      for(unsigned long j=0; j<i; j++)
        out[width-j-1] = out[i-j-1];
      for(unsigned long j=width-i; j--; )
        out[j] = filler;
      i = width;
    }
  }
  return i;
}

static unsigned long print_int_formatted(char*restrict out, unsigned long size, unsigned long long val, struct print_option*restrict opts){
  static const char digits[] = "0123456789ABCDEF";
  char buf[32]; // 20 is neough for a 64bit unsigned number. Added a bit more for other stuff, like signs, prefixes, and then some.
  int base = 10;
  if(opts->int_format == F_octal)
    base = 8;
  if(opts->int_format == F_hex || opts->int_format == F_HEX)
    base = 16;
  int n = sizeof(buf);
  buf[--n] = 0;
  BOOLEAN neg = 0;
  BOOLEAN sign = opts->force_sign;
  if(opts->is_signed && val > (((unsigned long long)~0)>>1)){
    sign = 1;
    neg = 1;
    val = -val;
  }
  do {
    int x = val % base;
    val /= base;
    const char digit = digits[x];
    buf[--n] = digit;
  } while(val);
  int m=0;
  if(!opts->pad_zero){
    if(opts->prefix && base == 16){
      buf[--n] = 'x';
      buf[--n] = '0';
    }
    if(opts->prefix && base == 8){
      buf[--n] = '0';
    }
    if(sign){
      buf[--n] = neg ? '-' : '+';
    }
  }else{
    if(sign && size){
      out[m++] = neg ? '-' : '+';
    }
    if(opts->prefix && base == 16 && size-m >= 2){
      out[m++] = '0';
      out[m++] = 'x';
    }
  }
  opts->precision = 0;
  if(opts->pad_zero){
    opts->left_justify = 0;
  }
  return print_string_formatted(out+m, size-m, &buf[n], opts)+m;
}

static unsigned long print_double_formatted(char*restrict out, unsigned long size, long double val, const struct print_option*restrict opts){
  // TODO
  return 0;
}

int myvsniprintf(char*restrict out, unsigned long size, unsigned max_args, const char*restrict fmt, VA_LIST ap){
  if(!fmt || !size)
    return 0;
  size -= 1;
  unsigned long off=0, in=0;
  unsigned ia = 0;
  while(off < size){
    while(1){
      if(off >= size)
        goto done;
      const char ch = fmt[in++];
      if(!ch) goto done;
      if(ch == '%'){
        break;
      }
      out[off++] = ch;
    }
    enum {
      T_INT,
      T_CHARNUM,
      T_SHORT,
      T_LONG,
      T_SIZE,
      T_PTRDIFF,
      T_LONG_LONG,
      T_MAXINT,
      T_POINTER,

      T_S_INT,
      T_S_CHARNUM,
      T_S_SHORT,
      T_S_LONG,
      T_S_SIZE,
      T_S_PTRDIFF,
      T_S_LONG_LONG,
      T_S_MAXINT,
      
      T_STRING,
      T_CHAR,
      T_DOUBLE,
      T_LONG_DOUBLE,
    } type = T_INT;
    BOOLEAN precision = 0;
    struct print_option opts = {0};
    ia += 1;
    if(ia <= max_args)
    while(1){
      if(off >= size)
        goto done;
      const char ch = fmt[in++];
      switch(ch){
        case 0: goto done;
        case 'h': type = fmt[in] == 'h' ? (in++, T_CHARNUM) : T_SHORT; continue;
        case 'l': type = fmt[in] == 'l' ? (in++, T_LONG_LONG) : T_LONG; continue;
        case 'j': type = T_MAXINT; continue;
        case 'z': type = T_SIZE; continue;
        case 't': type = T_PTRDIFF; continue;
        case 'L': type = T_LONG_DOUBLE; continue;
        case 'i':
        case 'd': opts.int_format = F_SIGNED; type += T_S_INT; break;
        case 'u': opts.int_format = F_UNSIGNED; break;
        case 'o': opts.int_format = F_octal; break;
        case 'x': opts.int_format = F_hex; break;
        case 'X': opts.int_format = F_HEX; break;
        case 'c': type = T_CHAR; break;
        case 's': type = T_STRING; break;
        case 'p': type = T_POINTER; break;
        case 'f': case 'F': case 'a': case 'A': case 'e': case 'E': {
          if(type != T_LONG_DOUBLE)
            type = T_DOUBLE;
        } break;
        case 'n': {
          int* ret = VA_ARG(ap, int*);
          *ret = off;
        } goto skip;
        case '#': opts.prefix = 1; continue;
        case '.': precision = 1; continue;
        case ' ': opts.sign_space_blank = 1; continue;
        case '+': opts.force_sign = 1; continue;
        case '-': opts.left_justify = 1; continue;
        case '%': out[off++] = '%'; goto skip;
        case '0': opts.pad_zero = 1; continue;
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': {
          in--;
          while(1){
            if(off >= size)
              goto done;
            char ch = fmt[in];
            if(!ch) goto done;
            if(ch < '0' || ch > '9')
              break;
            ch -= '0';
            in++;
            if(precision){
              opts.precision = opts.precision * 10 + (unsigned)ch;
            }else{
              opts.width = opts.width * 10 + (unsigned)ch;
            }
          }
        } continue;
        case '*': {
          ia += 1;
          if(ia > max_args)
            goto skip;
          const unsigned result = VA_ARG(ap, unsigned);
          if(precision){
            opts.precision = result;
          }else{
            opts.width = result;
          }
        } continue;
        default: goto error;
      }
      long long unsigned ival = 0;
      char* sval = 0;
      char cval = 0;
      long double dval = 0;
      switch(type){
        if(0){ // integers
          if(0){ // unsigned integers
            if(0){ case T_INT: ival = VA_ARG(ap, unsigned); }
            if(0){ case T_CHARNUM: ival = (unsigned char)VA_ARG(ap, unsigned); } // types < int wwill be integer promited
            if(0){ case T_SHORT: ival = (unsigned short)VA_ARG(ap, unsigned); }
            if(0){ case T_LONG: ival = VA_ARG(ap, unsigned long); }
            if(0){ case T_SIZE: ival = VA_ARG(ap, unsigned long); }
            if(0){ case T_PTRDIFF: ival = VA_ARG(ap, unsigned long); }
            if(0){ case T_LONG_LONG: ival = VA_ARG(ap, unsigned long long); }
            if(0){ case T_MAXINT: ival = VA_ARG(ap, unsigned long long); }
            if(0){ case T_POINTER: ival = (unsigned long long)VA_ARG(ap, void*);
              opts.prefix = 1;
              opts.int_format = F_hex;
            }
          }
          if(0){ // signed integers
            if(0){ case T_S_INT: ival = VA_ARG(ap, int); }
            if(0){ case T_S_CHARNUM: ival = VA_ARG(ap, int); } // types < int wwill be integer promited
            if(0){ case T_S_SHORT: ival = VA_ARG(ap, int); }
            if(0){ case T_S_LONG: ival = VA_ARG(ap, long); }
            if(0){ case T_S_SIZE: ival = VA_ARG(ap, long); }
            if(0){ case T_S_PTRDIFF: ival = VA_ARG(ap, long); }
            if(0){ case T_S_LONG_LONG: ival = VA_ARG(ap, long long); }
            if(0){ case T_S_MAXINT: ival = VA_ARG(ap, long long); }
            opts.is_signed = 1;
          }
          off += print_int_formatted(&out[off], size-off, ival, &opts);
        }
        if(0){ case T_STRING: sval = VA_ARG(ap, char*);
          off += print_string_formatted(&out[off], size-off, sval, &opts);
        }
        if(0){ case T_CHAR: cval = VA_ARG(ap, int);// types < int wwill be integer promited
          out[off++] = cval;
        }
        if(0){
          if(0){ case T_DOUBLE: dval = VA_ARG(ap, double); }
          if(0){ case T_LONG_DOUBLE: dval = VA_ARG(ap, long double); }
          off += print_double_formatted(&out[off], size-off, dval, &opts);
        }
      }
      break;
    } skip:;
  } done:;
  out[off] = 0;
  return off > (~0u>>1) ? (~0u>>1) : off;
error:
  out[off] = 0;
  return -1;
}


int myulogprintf(char*restrict out, unsigned long size, unsigned arg_count, UINT64 isarg64bitmask, const char*restrict fmt, A2_VA_LIST ap){
  if(!fmt || !size)
    return 0;
  if(arg_count > 64) arg_count = 64;
  size -= 1;
  unsigned long off=0, in=0;
  unsigned ia = 0;
  while(off < size){
    while(1){
      if(off >= size)
        goto done;
      const char ch = fmt[in++];
      if(!ch) goto done;
      if(ch == '%'){
        break;
      }
      out[off++] = ch;
    }
    enum {
      T_INT,
      T_S_INT,
      T_POINTER,      
      T_STRING,
      T_CHAR,
      T_DOUBLE,
    } type = T_INT;
    BOOLEAN precision = 0;
    struct print_option opts = {0};
    ia += 1;
    if(ia <= arg_count)
    while(1){
      if(off >= size)
        goto done;
      const char ch = fmt[in++];
      switch(ch){
        case 0: goto done;
        case 'h': case 'l': case 'j': case 'z': case 't': case 'L':
          continue;
        case 'i':
        case 'd': opts.int_format = F_SIGNED; type += T_S_INT; break;
        case 'u': opts.int_format = F_UNSIGNED; break;
        case 'o': opts.int_format = F_octal; break;
        case 'x': opts.int_format = F_hex; break;
        case 'X': opts.int_format = F_HEX; break;
        case 'c': type = T_CHAR; break;
        case 's': type = T_STRING; break;
        case 'p': type = T_POINTER; break;
        case 'f': case 'F': case 'a': case 'A': case 'e': case 'E': {
          type = T_DOUBLE;
        } break;
        case '#': opts.prefix = 1; continue;
        case '.': precision = 1; continue;
        case ' ': opts.sign_space_blank = 1; continue;
        case '+': opts.force_sign = 1; continue;
        case '-': opts.left_justify = 1; continue;
        case '%': out[off++] = '%'; goto skip;
        case '0': opts.pad_zero = 1; continue;
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': {
          in--;
          while(1){
            if(off >= size)
              goto done;
            char ch = fmt[in];
            if(!ch) goto done;
            if(ch < '0' || ch > '9')
              break;
            ch -= '0';
            in++;
            if(precision){
              opts.precision = opts.precision * 10 + (unsigned)ch;
            }else{
              opts.width = opts.width * 10 + (unsigned)ch;
            }
          }
        } continue;
        case '*': {
          ia += 1;
          if(ia > arg_count)
            goto skip;
          const unsigned long result = isarg64bitmask & (1lu<<(ia-2)) ? A2_VA_ARG(ap, UINT64) : A2_VA_ARG(ap, UINT32);
          if(precision){
            opts.precision = result;
          }else{
            opts.width = result;
          }
        } continue;
        default: goto error;
      }
      const BOOLEAN isarg64 = isarg64bitmask & (1lu<<(ia-1));
      UINT64 ival = 0;
      char cval = 0;
      long double dval = 0;
      switch(type){
        if(0){ // integers
          if(0){ // unsigned integers
            if(0){ case T_INT: ival = isarg64 ? A2_VA_ARG(ap, UINT64) : A2_VA_ARG(ap, UINT32); }
            if(0){ case T_POINTER: case_pointer: ival = isarg64 ? A2_VA_ARG(ap, INT64) : A2_VA_ARG(ap, INT32);
              opts.prefix = 1;
              opts.int_format = F_hex;
            }
          }
          if(0){ case T_S_INT: ival = isarg64 ? A2_VA_ARG(ap, INT64) : A2_VA_ARG(ap, INT32);
            opts.is_signed = 1;
          }
          off += print_int_formatted(&out[off], size-off, ival, &opts);
        }
        if(0){ case T_STRING:
          if(!isarg64)
            goto case_pointer; // Pointer to string unexpectedly only 32 bit big, only printing address
          const char* sval = A2_VA_ARG(ap, const char*);
          off += print_string_formatted(&out[off], size-off, sval, &opts);
        }
        if(0){ case T_CHAR: cval = ival = isarg64 ? A2_VA_ARG(ap, INT64) : A2_VA_ARG(ap, INT32);
          out[off++] = cval;
        }
        if(0){
          case T_DOUBLE: dval = A2_VA_ARG(ap, double); // float is promoted to double
          off += print_double_formatted(&out[off], size-off, dval, &opts);
        }
      }
      break;
    } skip:;
  } done:;
  out[off] = 0;
  return off > (~0u>>1) ? (~0u>>1) : off;
error:
  out[off] = 0;
  return -1;
}

int mysnprintf(char* out, unsigned long size, char* fmt, ...){
  VA_LIST ap;
  VA_START (ap, fmt);
  int ret = myvsniprintf(out, size, -1, fmt, ap);
  VA_END (ap);
  return ret;
}
