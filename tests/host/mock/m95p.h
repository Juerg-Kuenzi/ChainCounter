#pragma once
#include "Arduino.h"
#include <vector>
struct M95P {
  std::vector<uint8_t> m = std::vector<uint8_t>(1<<20, 0xFF);
  int programs=0, writes=0, erases=0; bool failNext=false; int tornBytes=-1;
  void read(uint32_t a, void* b, size_t l){ memcpy(b,&m[a],l); }
  bool pageProgram(uint32_t a,const void* b,size_t l,bool=false){ programs++;
    const uint8_t*p=(const uint8_t*)b; for(size_t i=0;i<l;i++){ if(m[a+i]!=0xFF){printf("PROGRAM ON NON-ERASED at %u!\n",a);exit(1);} }
    size_t n = tornBytes>=0 ? (size_t)tornBytes : l; tornBytes=-1; memcpy(&m[a],p,n); return n==l; }
  bool pageWrite(uint32_t a,const void* b,size_t l,bool=false){ writes++; memcpy(&m[a],b,l); return true; }
  bool pageErase(uint32_t a){ erases++; memset(&m[a&~511u],0xFF,512); return true; }
  bool sectorErase(uint32_t a){ memset(&m[a&~4095u],0xFF,4096); return true; }
  bool blockErase(uint32_t a){ memset(&m[a&~65535u],0xFF,65536); return true; }
};
