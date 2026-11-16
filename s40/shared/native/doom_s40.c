/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Nokia S40 runtime-resolved backend, GPL-2.0-or-later.
 * Once native_main starts, there are no KNI calls, Java callbacks, Canvas
 * operations, RMS, JSR-75, or Java clocks in the game loop.
 */
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "i_system.h"
#include "input.h"
#include "m_config.h"
#include "m_controls.h"
#include "s40/runtime.h"

static unsigned frames;
static unsigned display_width, display_height, draw_width, draw_height, draw_top;
static uint16_t sample_columns[320], sample_rows[240];
static uint32_t* pixels;
extern int saveStringEnter;

static void read_keys(void) {
    s40_event event;
    s40_pump();
    for (unsigned budget = 0; budget < 64 && s40_poll_event(&event); ++budget) {
        if (event.category != S40_EVENT_KEYBOARD)
            continue;
        s40_result[8] = event.value;
        s40_result[9] = event.type;
        enum s40_input_context context = saveStringEnter ? S40_INPUT_TEXT
                                         : menuactive    ? S40_INPUT_MENU
                                                         : S40_INPUT_GAME;
        s40_input_event((uint32_t)event.value, event.type, context);
    }
}
int DG_GetKey(int* pressed, unsigned char* key) {
    if (s40_input_next(pressed, key))
        return 1;
    read_keys();
    return s40_input_next(pressed, key);
}
uint32_t DG_GetTicksMs(void) { return s40_uptime_ms(); }
void DG_SleepMs(uint32_t ms) { s40_sleep_ms(ms); }
void DG_SetWindowTitle(const char* s) { printf("TITLE: %s\n", s); }
void DG_Init(void) {
    s40_result[4] = 3;
    if (!DG_ScreenBuffer)
        I_Error("Cannot allocate Doom render buffer");
    pixels = s40_display_open();
    display_width = s40_display_width();
    display_height = s40_display_height();
    draw_width = display_width;
    draw_height = draw_width * 3 / 4;
    draw_top = (display_height - draw_height) / 2;
    // Divide once per coordinate during setup, never once per pixel per frame.
    for (unsigned x = 0; x < draw_width; x++)
        sample_columns[x] = x * 320 / draw_width;
    for (unsigned y = 0; y < draw_height; y++)
        sample_rows[y] = y * 200 / draw_height;
    /* Show an immediate native border so a stall can be distinguished from
     * never reaching the LCD API. The center is replaced by the first frame. */
    for (unsigned i = 0; i < display_width * display_height; i++)
        pixels[i] = 0xFF102030u;
    s40_display_present();
    printf("Doom display initialized: %ux%u\n", display_width, display_height);
}
void DG_DrawFrame(void) {
    if (!pixels)
        return;
    /* Keep the original 320x200 render buffer; present at 4:3 on either LCD. */
    for (unsigned y = 0; y < draw_height; y++) {
        uint32_t* dst = pixels + (y + draw_top) * display_width;
        const uint32_t* src = DG_ScreenBuffer + sample_rows[y] * 320;
        /* Doom's RGB output has no opacity byte; supply it for the LCD path. */
        for (unsigned x = 0; x < draw_width; x++)
            dst[x] = src[sample_columns[x]] | 0xFF000000u;
    }
    if (!frames)
        puts("Doom frame 1 ready for display");
    s40_display_present();
    ++frames;
    if (frames == 1 || frames == 35)
        printf("Doom frame %u refresh returned\n", frames);
    s40_result[5] = frames;
    s40_result[4] = 6;
    s40_pump();
}
void I_Init(void) {}
void I_Tactile(int a, int b, int c) {
    (void)a;
    (void)b;
    (void)c;
}
boolean I_ConsoleStdout(void) { return true; }
void I_BindVariables(void) {}
void I_PrintBanner(char* s) { puts(s); }
void I_PrintStartupBanner(char* s) { puts(s); }
void I_PrintDivider(void) {}
void I_AtExit(atexit_func_t f, boolean on_error) { s40_on_exit(f, on_error); }
void I_Quit(void) { s40_finish(0); }
void I_Error(char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf((char*)(s40_result + 16), 256, fmt, ap);
    va_end(ap);
    printf("ERROR: %s\n", (char*)(s40_result + 16));
    s40_finish(-1);
}
byte* I_ZoneBase(int* size) {
    for (int mb = 6; mb >= 2; --mb) {
        byte* p = malloc(mb * 1024 * 1024);
        if (p) {
            *size = mb * 1024 * 1024;
            printf("zone %d MiB\n", mb);
            return p;
        }
    }
    I_Error("Cannot allocate 2 MiB Doom zone");
    return NULL;
}
boolean I_GetMemoryValue(unsigned offset, void* out, int size) {
    static const byte dos[10] = {0x57, 0x92, 0x19, 0, 0xf4, 6, 0x70, 0, 0x16, 0};
    if (size < 1 || size > 4 || offset > 10 - (unsigned)size)
        return false;
    memcpy(out, dos + offset, size);
    return true;
}
static const char* find_wad(void) {
    static const char* roots[] = {"D:\\doom\\", "C:\\predefgallery\\doom\\", "C:\\doom\\",
                                  "E:\\doom\\"};
    static const char* names[] = {"doom1.wad", "doom.wad", "doom2.wad", "freedoom1.wad",
                                  "freedoom2.wad"};
    for (unsigned i = 0; i < sizeof(roots) / sizeof(*roots); i++) {
        s40_set_root(roots[i]);
        for (unsigned j = 0; j < sizeof(names) / sizeof(*names); j++) {
            FILE* f = fopen(names[j], "rb");
            if (!f)
                continue;
            unsigned char header[12];
            size_t n = fread(header, 1, 12, f);
            fclose(f);
            if (n == 12 && !memcmp(header, "IWAD", 4))
                return names[j];
        }
    }
    return NULL;
}
/* Engine entry: the shared runtime owns session setup and cleanup. */
int s40_app_main(void) {
    s40_input_init(s40_input_layout());
    const char* wad = find_wad();
    if (!wad)
        I_Error("No IWAD. Put doom1.wad in D:\\doom\\ or C:\\predefgallery\\doom\\");
    s40_set_log(fopen("doom.log", "wb"));
    printf("Adaptive native Doom " S40_DOOM_VERSION "; %s%s\n", s40_get_root(), wad);
    // Successful bootstrap already requires privileged CP15 cache access.
    // Record the actual core ID/revision; MIDR does not encode clock speed.
    uint32_t midr;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
    printf("CPU MIDR %08x\n", midr);
    s40_result[4] = 4;
    /* Exit callbacks still inspect myargv after s40_finish longjmps out of
     * this function. Keep the pointer array alive until session cleanup. */
    static char* argv[] = {"doomgeneric", "-iwad",       NULL,           "-nosound", "-nogui",
                           "-config",     "default.cfg", "-extraconfig", "doom.cfg"};
    argv[2] = (char*)wad;
    doomgeneric_Create(sizeof(argv) / sizeof(*argv), argv);
    puts("Doom initialization complete");
    key_menu_confirm = KEY_ENTER;
    key_menu_abort = KEY_ESCAPE;
    key_nextweapon = S40_NEXT_WEAPON_KEY;
    key_prevweapon = S40_PREVIOUS_WEAPON_KEY;
    s40_result[4] = 5;
    for (;;)
        doomgeneric_Tick();
}
