#include <utils.h>

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
