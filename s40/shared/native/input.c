/* SPDX-License-Identifier: MIT */
/* Runtime keypad/QWERTY mapping. No Java callbacks or firmware memory reads. */
#include "input.h"

#include <string.h>

#include "doomkeys.h"
#include "s40/events.h"

enum {
    FIRST_SOURCE = -64,
    LAST_SOURCE = 127,
    SOURCE_COUNT = LAST_SOURCE - FIRST_SOURCE + 1,
    DOOM_KEY_COUNT = 256,
    QUEUE_SIZE = 128
};

static unsigned input_layout, fn_modifiers;
/* Retain each source's mapping at key-down. Menus, case and modifier layers
 * may change before key-up. Several sources can hold the same Doom key.
 */
static unsigned char source_keys[SOURCE_COUNT];
static unsigned short references[DOOM_KEY_COUNT];
static unsigned char delivered[DOOM_KEY_COUNT];
static struct {
    unsigned char key, down;
} events[QUEUE_SIZE];
static unsigned read_index, write_index;
static int resynchronize;

void s40_input_init(unsigned layout) {
    input_layout = layout;
    fn_modifiers = 0;
    read_index = write_index = 0;
    resynchronize = 0;
    memset(source_keys, 0, sizeof(source_keys));
    memset(references, 0, sizeof(references));
    memset(delivered, 0, sizeof(delivered));
}

static void queue_transition(unsigned char key, int down) {
    if (resynchronize)
        return;
    if (write_index - read_index == QUEUE_SIZE) {
        /* Preserve the desired held state and what Doom has actually seen.
         * Under a flood, discard queued taps and reconcile those two states.
         * Simply clearing the queue would strand an already delivered press.
         */
        read_index = write_index = 0;
        resynchronize = 1;
        return;
    }
    events[write_index % QUEUE_SIZE].key = key;
    events[write_index % QUEUE_SIZE].down = down;
    ++write_index;
}

static void release_source(unsigned index) {
    unsigned char key = source_keys[index];
    if (key) {
        source_keys[index] = 0;
        if (--references[key] == 0)
            queue_transition(key, 0);
    }
}

static void release_printable_sources(void) {
    /* Fn can turn one physical key into two unrelated character codes.
     * The queue exposes translated characters, not stable physical IDs.
     * Stop printable holds at a layer change; the user can press them again.
     * D-pad and Shift holds remain independent of that layer.
     */
    for (int code = ' '; code <= LAST_SOURCE; ++code)
        release_source(code - FIRST_SOURCE);
}

static unsigned char map_key(int code, enum s40_input_context context) {
    int selecting = context != S40_INPUT_GAME;
    switch (code) {
        case S40_KEYCODE_UP:
            return KEY_UPARROW;
        case S40_KEYCODE_DOWN:
            return KEY_DOWNARROW;
        case S40_KEYCODE_LEFT:
            return KEY_LEFTARROW;
        case S40_KEYCODE_RIGHT:
            return KEY_RIGHTARROW;
        case S40_KEYCODE_SELECT:
            return selecting ? KEY_ENTER : KEY_FIRE;
        case S40_KEYCODE_SOFT_LEFT:
            return KEY_ENTER;
        case S40_KEYCODE_RETURN:
            return input_layout == S40_INPUT_QWERTY && !selecting ? KEY_USE : KEY_ENTER;
        case S40_KEYCODE_SOFT_RIGHT:
            return KEY_ESCAPE;
        case S40_KEYCODE_BACKSPACE:
            return selecting ? KEY_BACKSPACE : KEY_ESCAPE;
    }
    if (input_layout == S40_INPUT_QWERTY) {
        if (code == S40_KEYCODE_SHIFT)
            return KEY_RSHIFT;
        if (context == S40_INPUT_TEXT)
            return code >= ' ' && code < S40_KEYCODE_DELETE ? code : 0;
        // D-pad is primary. Space/Enter and the I/O/K/L/P cluster put the
        // remaining actions under the other thumb on both QWERTY and QWERTZ.
        // WASD and the older letter bindings remain optional aliases.
        switch (code) {
            case 'w':
                return KEY_UPARROW;
            case 's':
                return KEY_DOWNARROW;
            case 'a':
                return KEY_LEFTARROW;
            case 'd':
                return KEY_RIGHTARROW;
            case ' ':
                return selecting ? KEY_ENTER : KEY_FIRE;
            case KEY_ESCAPE:
                return KEY_ESCAPE;
            case KEY_TAB:
                return KEY_TAB;
        }
        if (context == S40_INPUT_MENU)
            return code >= ' ' && code < S40_KEYCODE_DELETE ? code : 0;
        switch (code) {
            case 'i':
            case 'q':
                return KEY_STRAFE_L;
            case 'o':
            case 'r':
                return KEY_STRAFE_R;
            case 'e':
                return KEY_USE;
            case 'k':
            case 'c':
                return KEY_RSHIFT;
            case 'l':
            case 'm':
                return KEY_TAB;
            case 'p':
            case 'n':
                return S40_NEXT_WEAPON_KEY;
            case 'b':
                return S40_PREVIOUS_WEAPON_KEY;
            default:
                return code >= '1' && code <= '8' ? code : 0;
        }
    }
    // Numeric phones retain their original controls. Letters are useful when
    // testing them with an emulator keyboard, but do not replace the keypad.
    switch (code) {
        case '2':
        case 'w':
            return KEY_UPARROW;
        case '8':
        case 's':
            return KEY_DOWNARROW;
        case '4':
        case 'a':
            return KEY_LEFTARROW;
        case '6':
        case 'd':
            return KEY_RIGHTARROW;
        case '5':
        case ' ':
            return selecting ? KEY_ENTER : KEY_FIRE;
        case '0':
        case 'e':
            return KEY_USE;
        case '1':
            return KEY_STRAFE_L;
        case '3':
            return KEY_STRAFE_R;
        case '7':
            return KEY_RSHIFT;
        case '9':
            return KEY_TAB;
        case '#':
            return S40_NEXT_WEAPON_KEY;
        case '*':
            return KEY_ESCAPE;
        default:
            return 0;
    }
}

void s40_input_event(uint32_t value, int type, enum s40_input_context context) {
    if (type != S40_KEY_DOWN && type != S40_KEY_UP)
        return;
    /* Low 16 bits are the signed key code. In C3's upper half, bits 16..20
     * identify a rotating history slot, while bits 24..31 carry modifiers.
     * Neither the complete packed value nor that history slot is a key ID.
     */
    int code = (int16_t)value;
    if (input_layout == S40_INPUT_QWERTY) {
        unsigned modifiers = (value >> 24) & S40_MODIFIER_FN_MASK;
        if (modifiers != fn_modifiers || code == S40_KEYCODE_FN)
            release_printable_sources();
        fn_modifiers = modifiers;
        if (code == S40_KEYCODE_FN)
            return;
    }
    if (code >= 'A' && code <= 'Z')
        code += 'a' - 'A';
    if (code == S40_KEYCODE_LINE_FEED)
        code = S40_KEYCODE_RETURN;
    if (code == S40_KEYCODE_DELETE)
        code = S40_KEYCODE_BACKSPACE;
    if (code < FIRST_SOURCE || code > LAST_SOURCE)
        return;
    unsigned index = code - FIRST_SOURCE;
    if (type == S40_KEY_UP) {
        release_source(index);
    } else if (!source_keys[index]) {
        unsigned char key = map_key(code, context);
        if (key) {
            source_keys[index] = key;
            if (references[key]++ == 0)
                queue_transition(key, 1);
        }
    }
}

int s40_input_next(int* pressed, unsigned char* key) {
    if (resynchronize) {
        // Releases first, then presses, so modifier/held state cannot stick.
        for (int down = 0; down <= 1; ++down) {
            for (unsigned k = 1; k < DOOM_KEY_COUNT; ++k) {
                if ((references[k] != 0) == down && delivered[k] != down) {
                    delivered[k] = down;
                    *pressed = down;
                    *key = k;
                    return 1;
                }
            }
        }
        resynchronize = 0;
    }
    if (read_index == write_index)
        return 0;
    *key = events[read_index % QUEUE_SIZE].key;
    *pressed = events[read_index % QUEUE_SIZE].down;
    delivered[*key] = *pressed;
    ++read_index;
    return 1;
}
