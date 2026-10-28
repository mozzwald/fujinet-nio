/* Standalone RAM diagnostic: read-only Springboard operations, explicit buttons.
 * Mapping comes from installed Module B receipt + HsCardAttrGet, never literals.
 * No timing setter, reset, peripheral write, NIO or code execution from CS1. */
#include <PalmOS.h>
#define CREATOR 'FNFR'
#define LOGNAME "FN Fixed Read E2 Log"
Err HsGet(UInt16,UInt16,void *) __attribute__((__callseq__("move.w #6,-(%%sp); trap #15; dc.w 0xA349")));
MemPtr *HsCatch(void) __attribute__((__callseq__("move.w #9,-(%%sp); trap #15; dc.w 0xA349")));
extern UInt32 df_pc(void),df_read_pair(const void *);
extern UInt16 df_read_word(const void *);
static UInt32 result[24];
static const Char *message="Idle: no bus reads yet";
static UInt32 be32(const UInt8 *p){return ((UInt32)p[0]<<24)|((UInt32)p[1]<<16)|((UInt32)p[2]<<8)|p[3];}
static UInt32 crc(const UInt8 *p,UInt32 n){UInt32 c=0xffffffffUL;UInt16 k;while(n--){c^=*p++;for(k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320UL:0);}return ~c;}
static Err receipt(UInt16 *card,UInt32 *generation){UInt8 b[128];UInt16 n=sizeof(b);
 if(PrefGetAppPreferences('FNMC',1,b,&n,true)!=1||n!=128||be32(b)!=0x46525032UL||be32(b+4)!=1||!be32(b+96)||be32(b+24)||be32(b+104)!=7)return 1;
 *card=(UInt16)be32(b+16);*generation=be32(b+12);return !*card||*card>3||!*generation?1:0;
}
static Err guard(void){UInt16 card,a;UInt32 generation,base=0,size=0,access=0;UInt8 flag;Err e;
 e=receipt(&card,&generation);if(e)return e;
 if(card!=result[5]||generation!=result[6])return 1;
 for(a=0;a<3;++a){flag=0;e=HsGet(card,a,&flag);if(e||!flag)return e?e:2;}
 flag=1;e=HsGet(card,6,&flag);if(e||flag)return e?e:2;
 flag=0;e=HsGet(card,15,&flag);if(e||!flag)return e?e:2;
 e=HsGet(card,3,&base);if(!e)e=HsGet(card,4,&size);if(!e)e=HsGet(card,5,&access);
 return e?e:(base!=result[7]||size!=result[8]||access!=result[10])?2:0;
}
static Err prepare(void){UInt16 card=0,appcard,index,k;UInt32 gen=0,hs;LocalID id;DmOpenRef db;MemHandle h;MemPtr p;Err e;
 MemSet(result,sizeof(result),0);result[0]=0x46444632UL;result[1]=1;
 if(FtrGet('hsEx',0,&hs))return 1;e=receipt(&card,&gen);if(e)return e;result[5]=card;result[6]=gen;
 e=HsGet(card,3,&result[7]);if(!e)e=HsGet(card,4,&result[8]);if(!e)e=HsGet(card,5,&result[10]);if(e)return e;
 if(!result[7]||(result[7]&0xffffffUL)||result[8]!=0x1000000UL||result[7]>0xffffffffUL-result[8]||result[10]!=330)return 3;
 result[9]=result[7]+result[8];if(result[9]>0xffffffffUL-8)return 3;
 e=guard();if(e)return e;
 if(SysCurAppDatabase(&appcard,&id)||appcard)return 4;
 db=DmOpenDatabase(0,id,dmModeReadOnly);if(!db)return 4;index=DmFindResource(db,'code',1,0);h=index==0xffff?0:DmGetResourceIndex(db,index);p=h?MemHandleLock(h):0;
 result[11]=df_pc();if(p){UInt16 heap=MemHandleHeapID(h);for(k=0;k<MemNumRAMHeaps(0);++k)if(MemHeapID(0,k)==heap&&!(MemHeapFlags(heap)&1)&&result[11]>=(UInt32)p&&result[11]-(UInt32)p<MemHandleSize(h))result[20]=1;MemHandleUnlock(h);}
 if(h)DmReleaseResource(h);DmCloseDatabase(db);return result[20]?0:4;
}
static void check(UInt32 actual,UInt32 expected){result[16]=actual;if(actual!=expected){if(!result[17]){result[18]=expected;result[19]=actual;}++result[17];}}
static Err exercise(UInt16 mode){ErrExceptionType frame;MemPtr *volatile list;volatile Err e=0;UInt32 start=TimGetTicks();UInt16 i;
 result[3]=mode;result[12]=mode==1?1:mode==4?4:100;
 list=HsCatch();if(!list)return 5;frame.err=0;frame.nextP=(ErrExceptionPtr)*list;*list=(MemPtr)&frame;
 if(ErrSetJump(frame.state)==0){for(i=0;i<result[12];++i){
  if(mode==3){check(df_read_word((const void *)(result[7]+8)),0xfeed);++result[14];}
  if(mode==4){check(df_read_pair((const void *)(result[7]+8)),0xfeedbeefUL);result[14]+=2;}
  else {check(df_read_word((const void *)result[9]),0xbeef);++result[15];}
  ++result[13];
 }}else e=5;
 *list=(MemPtr)frame.nextP;result[21]=TimGetTicks()-start;if(!e)e=guard();if(!e&&result[17])e=6;return e;
}
static Err save(void){LocalID id;UInt16 attr=0,index;UInt32 type=0,creator=0;DmOpenRef db;MemHandle h;MemPtr p;Err e;
 id=DmFindDatabase(0,LOGNAME);if(!id){e=DmCreateDatabase(0,LOGNAME,CREATOR,'DATA',false);if(e)return e;id=DmFindDatabase(0,LOGNAME);}
 e=DmDatabaseInfo(0,id,0,&attr,0,0,0,0,0,0,0,&type,&creator);if(e||type!='DATA'||creator!=CREATOR||(attr&1))return 7;
 db=DmOpenDatabase(0,id,dmModeReadWrite);if(!db)return 7;index=DmNumRecords(db);if(index>=64){DmCloseDatabase(db);return 7;}
 result[2]=index+1;result[22]=TimGetSeconds();result[23]=crc((const UInt8 *)result,92);
 h=DmNewRecord(db,&index,sizeof(result));p=h?MemHandleLock(h):0;if(!p){if(h)DmReleaseRecord(db,index,false);DmCloseDatabase(db);return 7;}
 e=DmWrite(p,0,result,sizeof(result));MemHandleUnlock(h);{Err re=DmReleaseRecord(db,index,e==0);if(!e)e=re;}if(e)DmRemoveRecord(db,index);DmCloseDatabase(db);return e;
}
static void hex(Coord y,const Char *label,UInt32 value){Char b[42],n[10];StrCopy(b,label);StrIToH(n,value);StrCat(b,n);WinDrawChars(b,StrLen(b),2,y);}
static void draw(void){RectangleType box;RctSetRectangle(&box,0,16,160,96);WinEraseRectangle(&box,0);WinDrawChars(message,StrLen(message),2,17);hex(29,"Err ",result[4]);hex(41,"RAM PC ",result[11]);hex(53,"CS0 ",result[7]);hex(65,"CS1 ",result[9]);hex(77,"Value ",result[16]);hex(89,"Done ",result[13]);hex(101,"Bad ",result[17]);}
UInt32 PilotMain(UInt16 cmd,MemPtr param,UInt16 flags){EventType event;UInt16 error;(void)param;(void)flags;if(cmd!=sysAppLaunchCmdNormalLaunch)return 0;FrmGotoForm(1000);
 do{EvtGetEvent(&event,evtWaitForever);if(!SysHandleEvent(&event)&&!MenuHandleEvent(0,&event,&error)){
  if(event.eType==frmLoadEvent)FrmSetActiveForm(FrmInitForm(event.data.frmLoad.formID));
  else if(event.eType==frmOpenEvent){FrmDrawForm(FrmGetActiveForm());result[4]=prepare();draw();}
  else if(event.eType==ctlSelectEvent&&event.data.ctlSelect.controlID>=1001&&event.data.ctlSelect.controlID<=1004){UInt16 mode=event.data.ctlSelect.controlID-1000;Err e;result[4]=prepare();result[3]=mode;if(!result[4])result[4]=exercise(mode);e=save();if(e)result[4]=e;message=result[4]?"FAIL: keep result":"PASS: read-only test";SndPlaySystemSound(result[4]?sndError:sndConfirmation);draw();}
  else FrmHandleEvent(FrmGetActiveForm(),&event);
 }}while(event.eType!=appStopEvent);FrmCloseAllForms();return 0;}
