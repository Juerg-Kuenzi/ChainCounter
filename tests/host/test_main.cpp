#include "recordlog.h"
#include "records.h"
#include "nmea.h"
#include "crc32.h"
#include <assert.h>
#include <stddef.h>
// FW 1.0.0 hatte an Offset 16 reserved[0] = 0 -> alte Records liefern TotalRuntime 0
static_assert(offsetof(CounterRecord, totalRuntime) == 16, "Layout-Kompatibilitaet");
int main(){
  // CRC: bekannter Wert fuer "123456789" mit Standard-Init 0xFFFFFFFF (ohne final XOR) = ~0xCBF43926
  assert(crc32_calc("123456789",9,0xFFFFFFFFu) == (0xCBF43926u ^ 0xFFFFFFFFu));
  printf("CRC ok, spec-start crc(\"123456789\")=%08X\n", crc32_calc("123456789",9));
  // NMEA
  char s[64]="$ISLWNS,K,0.42"; nmeaAppendChecksum(s,sizeof s); printf("built: %s", s);
  char l1[64]; strcpy(l1,s); l1[strlen(l1)-2]=0; char*f[8]; int n; const char*e;
  assert(nmeaParse(l1,f,8,&n,&e) && n==3 && !strcmp(f[0],"ISLWNS") && !strcmp(f[2],"0.42"));
  char lr[]="$ISLWNS,R*62"; assert(nmeaParse(lr,f,8,&n,&e) && n==2 && !strcmp(f[0],"ISLWNS") && !strcmp(f[1],"R"));
  char l2[]="$ISLWNS,C*00"; assert(!nmeaParse(l2,f,8,&n,&e)); printf("bad cs: %s\n",e);
  char l3[]="$ISLWNS,C"; assert(!nmeaParse(l3,f,8,&n,&e));
  long v; float fl; assert(parseIntStrict("12",&v)&&v==12); assert(!parseIntStrict("12a",&v)); assert(!parseIntStrict("",&v)); assert(!parseIntStrict(" 1",&v));
  assert(parseFloatStrict("0.4",&fl)); assert(!parseFloatStrict("0.4x",&fl)); assert(!parseFloatStrict("nan",&fl));
  // RecordLog
  M95P ee; RecordLog log(ee,0,0x10000,32,0x434E5452);
  CounterRecord r{}; assert(!log.scan(&r));
  int32_t value=0;
  for(int boot=0; boot<3000; boot++){           // 3000 Starts -> Ring laeuft ueber (2048 Slots)
    RecordLog L(ee,0,0x10000,32,0x434E5452); CounterRecord c{};
    bool found=L.scan(&c);
    if(boot>0){ assert(found && c.revolutions==value); }
    if(boot>0){ assert(c.totalRuntime==(uint32_t)(boot-1)*10); }
    L.prepareNext(); c.revolutions=value; c.totalRuntime=boot*10; assert(L.append(&c)); L.prepareNext();   // Start-Record
    value+= 7; c={}; c.revolutions=value; c.totalRuntime=boot*10; assert(L.append(&c,true));  // Power-fail-Record
  }
  printf("3000 Zyklen ok, programs=%d writes=%d erases=%d\n",ee.programs,ee.writes,ee.erases);
  // abgebrochener Schreibvorgang beim Stromausfall
  { RecordLog L(ee,0,0x10000,32,0x434E5452); CounterRecord c{}; assert(L.scan(&c)); int32_t old=c.revolutions;
    L.prepareNext(); ee.tornBytes=10; c={}; c.revolutions=old+1; L.append(&c,true);
    RecordLog L2(ee,0,0x10000,32,0x434E5452); assert(L2.scan(&c) && c.revolutions==old); assert(L2.prepareNext());
    c={}; c.revolutions=old+2; assert(L2.append(&c,true));
    RecordLog L3(ee,0,0x10000,32,0x434E5452); assert(L3.scan(&c) && c.revolutions==old+2);
    printf("Abgebrochener Write: alter Wert erhalten, danach normal weiter ok\n"); }
  printf("ALLE TESTS OK\n");
}
