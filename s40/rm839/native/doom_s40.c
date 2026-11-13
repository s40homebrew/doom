/* Nokia 301 Dual SIM RM-839 02.33 platform backend, GPL-2.0-or-later.
 * Once native_main starts, there are no KNI calls, Java callbacks, Canvas
 * operations, RMS, JSR-75, or Java clocks in the game loop.
 */
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "m_controls.h"
#include "m_config.h"
#include "i_system.h"

uint32_t *s40_result;
static jmp_buf finish_env;
static int finishing, app_id;
static unsigned frames, pump_depth;
static uint32_t start_ms;
static uint32_t *lcd;
static void *canvas;
static struct { unsigned char key,down; } keys[128];
static unsigned key_read,key_write;
static unsigned char held[256];
static struct {atexit_func_t fn;boolean on_error;} exit_fns[24];
static unsigned exit_count;

void s40_finish(int status) { longjmp(finish_env,status?status:1000); }

/* Mirror the native dispatch at 808A2452..808A24AA, never its Java
 * scheduler at 808A1460. RM-839 timer tokens are 192..441, unlike RM-443.
 * Leave the stock stack anchor at 60C3F804 unchanged. Queue count before
 * receive prevents an empty-queue blocking call from stalling Doom. */
void s40_pump(void) {
    if(pump_depth++) {--pump_depth;return;}
    for(unsigned budget=0;budget<32 && fw_msg_count();budget++) {
        uintptr_t msg=fw_os_msg_receive();
        if(!msg)continue;
        if(msg>=192 && msg<=441) {
            unsigned timer=(unsigned)msg-192;
            if(!fw_vm_timer((unsigned)msg))fw_CM_Timer_Timeout(timer);
            else if(timer==73)fw_debug_timer();
        } else if(*(volatile unsigned char *)msg!=1) {
            fw_CM_ComMgr_ProcessPhonetMessage((void *)msg);
        }
        fw_CM_ComMgr_ProcessInternalQueue();
    }
    fw_CM_ComMgr_ProcessInternalQueue();
    fw_CM_ComMgr_ScanISISubscriptions();
    --pump_depth;
}
static void enqueue(unsigned char k,int down) {
    if(!k || held[k]==down)return;
    /* Do not overwrite releases: consumers drain at every Doom tic. */
    if(key_write-key_read>=128) {key_read=key_write;memset(held,0,sizeof(held));return;}
    held[k]=down;keys[key_write&127].key=k;keys[key_write&127].down=down;++key_write;
}
static void read_keys(void) {
    int category,type,value;
    s40_pump();
    for(unsigned budget=0;budget<64 && fw_event_pop(app_id,&category,&type,&value);budget++) {
        if(category!=1) {
            /* Native UI focus loss: return to launcher instead of drawing over
             * a phone dialog. The user can launch a fresh game afterwards. */
            if(category==2 && type==4)s40_finish(0);
            continue;
        }
        if(type!=1 && type!=3)continue; /* discard native key-repeat events */
        int code=(int16_t)value,down=(type==1);unsigned char key=0;
        if(s40_result) {s40_result[8]=code;s40_result[9]=type;}
        switch(code) {
        case -1: case '2':key=KEY_UPARROW;break;
        case -2: case '8':key=KEY_DOWNARROW;break;
        case -3: case '4':key=KEY_LEFTARROW;break;
        case -4: case '6':key=KEY_RIGHTARROW;break;
        case -5: case '5':key=KEY_FIRE;enqueue(KEY_ENTER,down);break;
        case -6:key=KEY_ENTER;break;
        case -7: case '*':key=KEY_ESCAPE;break;
        case '0':key=KEY_USE;break;
        case '1':key=',';break;
        case '3':key='.';break;
        case '7':key=KEY_RSHIFT;break;
        case '9':key=KEY_TAB;break;
        case '#': /* cycle weapons on a press, release the same key */
            if(down) {
                player_t *p=&players[consoleplayer];
                for(int i=1;i<=NUMWEAPONS;i++) {
                    int w=(p->readyweapon+i)%NUMWEAPONS;
                    if(p->weaponowned[w]) {
                        unsigned char k=w==wp_supershotgun?'3':'1'+w;
                        enqueue(k,1);enqueue(k,0);break;
                    }
                }
            }break;
        default:break;
        }
        enqueue(key,down);
    }
}
int DG_GetKey(int *pressed,unsigned char *key) {
    if(key_read==key_write)read_keys();
    if(key_read==key_write)return 0;
    *key=keys[key_read&127].key;*pressed=keys[key_read&127].down;++key_read;return 1;
}
uint32_t DG_GetTicksMs(void) {return fw_os_current_time_read_in_ms()-start_ms;}
void DG_SleepMs(uint32_t ms) {
    uint32_t now=fw_os_current_time_read_in_ms();
    do {s40_pump();}while((uint32_t)(fw_os_current_time_read_in_ms()-now)<ms);
}
void DG_SetWindowTitle(const char *s) {printf("TITLE: %s\n",s);}
void DG_Init(void) {
    s40_result[4]=3;
    /* Wrap externally owned ARGB pixels in a real 02.33 GDI context. This
     * mirrors Pixmap's native RGB-buffer wrapper (635E2716), flags 0x85.
     * Native refresh blits synchronously into the foreground UI surface, then
     * wakes JAVA_PAINTER_TASK to flush it. Neither path executes Java bytecode.
     * The context must be destroyed before freeing its external pixel buffer. */
    if(!*(volatile uintptr_t *)0x60352DF8u)I_Error("No foreground native surface");
    lcd=malloc(240*320*sizeof(*lcd));if(!lcd)I_Error("LCD allocation failed");
    for(unsigned i=0;i<240*320;i++)lcd[i]=0xFF102030u;
    int error=0;
    canvas=fw_gdi_wrap_pixels(&error,12,240,320,0x85,240*4,lcd);
    if(error || !canvas)I_Error("GDI context failed: %d",error);
    fw_ui_refresh(0,0,240,320,canvas,0,0);
}
void DG_DrawFrame(void) {
    if(!lcd)return;
    /* Doom renders its original 320x200. Present in 4:3 at 240x180 on the
     * portrait LCD; fixed integer sampling leaves engine geometry unchanged. */
    for(unsigned y=0;y<180;y++) {
        uint32_t *dst=lcd+(y+70)*240;
        const uint32_t *src=DG_ScreenBuffer+(y*200/180)*320;
        /* Doom's RGB output has no opacity byte; supply it for the LCD path. */
        for(unsigned x=0;x<240;x++)dst[x]=src[x*320/240]|0xFF000000u;
    }
    fw_ui_refresh(0,0,240,320,canvas,0,0);
    ++frames;s40_result[5]=frames;s40_result[4]=6;
    s40_pump();
}
void I_Init(void) {}
void I_Tactile(int a,int b,int c) {(void)a;(void)b;(void)c;}
boolean I_ConsoleStdout(void) {return true;}
void I_BindVariables(void) {}
void I_PrintBanner(char *s) {puts(s);} void I_PrintStartupBanner(char *s) {puts(s);} void I_PrintDivider(void) {}
void I_AtExit(atexit_func_t f,boolean on_error) {if(exit_count<24){exit_fns[exit_count].fn=f;exit_fns[exit_count++].on_error=on_error;}}
void I_Quit(void) {s40_finish(0);}
void I_Error(char *fmt,...) {
    va_list ap;va_start(ap,fmt);vsnprintf((char *)(s40_result+16),256,fmt,ap);va_end(ap);
    printf("ERROR: %s\n",(char *)(s40_result+16));s40_finish(-1);
}
byte *I_ZoneBase(int *size) {
    for(int mb=6;mb>=2;--mb) {byte *p=malloc(mb*1024*1024);if(p){*size=mb*1024*1024;printf("zone %d MiB\n",mb);return p;}}
    I_Error("Cannot allocate 2 MiB Doom zone");return NULL;
}
boolean I_GetMemoryValue(unsigned offset,void *out,int size) {
    static const byte dos[10]={0x57,0x92,0x19,0,0xf4,6,0x70,0,0x16,0};
    if(size<1 || size>4 || offset>10-(unsigned)size)return false;
    memcpy(out,dos+offset,size);return true;
}
static const char *find_wad(void) {
    static const char *roots[]={"D:\\doom\\","C:\\predefgallery\\doom\\","C:\\doom\\","E:\\doom\\"};
    static const char *names[]={"doom1.wad","doom.wad","doom2.wad","freedoom1.wad","freedoom2.wad"};
    for(unsigned i=0;i<sizeof(roots)/sizeof(*roots);i++) {
        s40_set_root(roots[i]);
        for(unsigned j=0;j<sizeof(names)/sizeof(*names);j++) {
            FILE *f=fopen(names[j],"rb");if(!f)continue;
            unsigned char header[12];size_t n=fread(header,1,12,f);fclose(f);
            if(n==12&&!memcmp(header,"IWAD",4))return names[j];
        }
    }
    return NULL;
}
/* Called on a private native stack by loader.S. The descriptor is a pinned
 * Java int[] only for returning diagnostics after exit, never a per-frame API.
 */
__attribute__((visibility("default"))) void native_main(uint32_t *result) {
    s40_result=result;s40_result[0]=0x444F4F4D;s40_result[4]=1;
    s40_result[6]=fw_os_task_own_id_read();
    app_id=fw_app_id();s40_result[7]=app_id;
    start_ms=fw_os_current_time_read_in_ms();
    int status=setjmp(finish_env);
    if(!status) {
        if(app_id<=0)I_Error("No native application ID");
        s40_result[4]=2;s40_pump();
        const char *wad=find_wad();
        if(!wad)I_Error("No IWAD. Put doom1.wad in D:\\doom\\ or C:\\predefgallery\\doom\\");
        s40_set_log(fopen("doom.log","wb"));
        printf("RM839 02.33 native Doom; task %u app %d; %s%s\n",s40_result[6],app_id,s40_get_root(),wad);
        s40_result[4]=4;
        char *argv[]={"doomgeneric","-iwad",(char *)wad,"-nosound","-nogui","-config","default.cfg","-extraconfig","doom.cfg"};
        doomgeneric_Create(sizeof(argv)/sizeof(*argv),argv);
        key_menu_confirm=KEY_ENTER;key_menu_abort=KEY_ESCAPE;
        s40_result[4]=5;
        for(;;)doomgeneric_Tick();
    }
    /* setjmp returns here on quit/error; avoid re-running exit callbacks if
     * one itself raises I_Error. Memory is still reclaimed in that case. */
    if(!finishing) {
        finishing=1;
        while(exit_count) {--exit_count;if(status==1000||exit_fns[exit_count].on_error)exit_fns[exit_count].fn();}
    }
    if(canvas) {fw_gdi_free_context(canvas);canvas=NULL;}
    printf("Exit %d, frames %u, ms %u\n",status,frames,DG_GetTicksMs());
    s40_result[3]=status==1000?0:status;s40_result[10]=DG_GetTicksMs();
    s40_cleanup();
}
