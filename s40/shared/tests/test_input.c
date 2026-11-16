/* SPDX-License-Identifier: MIT */
/* Host regressions for the C3 translated-event contract; no firmware execution. */
#include <assert.h>
#include <stdio.h>

#include "doomkeys.h"
#include "input.h"

static uint32_t packed(int code, unsigned slot, unsigned modifiers) {
    return (uint16_t)code | (slot << 16) | (modifiers << 24);
}

static void expect(int down, unsigned char expected) {
    int pressed;
    unsigned char key;
    assert(s40_input_next(&pressed, &key));
    assert(pressed == down && key == expected);
}

static void empty(void) {
    int pressed;
    unsigned char key;
    assert(!s40_input_next(&pressed, &key));
}

static void event(int code, int type, enum s40_input_context context) {
    s40_input_event(packed(code, 19, 0), type, context);
}

int main(void) {
    s40_input_init(S40_INPUT_QWERTY);
    /* History slots differ between press/release and aren't physical key IDs. */
    s40_input_event(packed(S40_KEYCODE_UP, 31, 0), S40_KEY_DOWN, S40_INPUT_GAME);
    expect(1, KEY_UPARROW);
    s40_input_event(packed(S40_KEYCODE_UP, 3, 0), S40_KEY_UP, S40_INPUT_GAME);
    expect(0, KEY_UPARROW);
    empty();

    /* D-pad and WASD aliases must coexist without releasing each other. */
    event(S40_KEYCODE_UP, S40_KEY_DOWN, S40_INPUT_GAME);
    event('W', S40_KEY_DOWN, S40_INPUT_GAME);
    expect(1, KEY_UPARROW);
    event(S40_KEYCODE_UP, S40_KEY_UP, S40_INPUT_GAME);
    empty();
    event('w', S40_KEY_UP, S40_INPUT_GAME);
    expect(0, KEY_UPARROW);

    /* Release the original action even if a menu opens during the hold. */
    event(' ', S40_KEY_DOWN, S40_INPUT_GAME);
    expect(1, KEY_FIRE);
    event(' ', S40_KEY_REPEAT, S40_INPUT_MENU);
    empty();
    event(' ', S40_KEY_UP, S40_INPUT_MENU);
    expect(0, KEY_FIRE);
    event(' ', S40_KEY_DOWN, S40_INPUT_MENU);
    expect(1, KEY_ENTER);
    event(' ', S40_KEY_UP, S40_INPUT_MENU);
    expect(0, KEY_ENTER);

    /* Fn releases printable holds but retains independent D-pad/Shift holds. */
    event('i', S40_KEY_DOWN, S40_INPUT_GAME);
    event(S40_KEYCODE_RIGHT, S40_KEY_DOWN, S40_INPUT_GAME);
    event(S40_KEYCODE_SHIFT, S40_KEY_DOWN, S40_INPUT_GAME);
    expect(1, KEY_STRAFE_L);
    expect(1, KEY_RIGHTARROW);
    expect(1, KEY_RSHIFT);
    s40_input_event(packed(S40_KEYCODE_FN, 4, 2), S40_KEY_DOWN, S40_INPUT_GAME);
    expect(0, KEY_STRAFE_L);
    empty();
    s40_input_event(packed(S40_KEYCODE_RIGHT, 5, 2), S40_KEY_UP, S40_INPUT_GAME);
    s40_input_event(packed(S40_KEYCODE_SHIFT, 6, 2), S40_KEY_UP, S40_INPUT_GAME);
    expect(0, KEY_RIGHTARROW);
    expect(0, KEY_RSHIFT);

    /* Primary C3 actions, including Enter and Backspace aliases. */
    s40_input_init(S40_INPUT_QWERTY);
    const int codes[] = {'o', 'k', 'l', 'p', S40_KEYCODE_LINE_FEED, S40_KEYCODE_DELETE};
    const unsigned char keys[] = {KEY_STRAFE_R,        KEY_RSHIFT, KEY_TAB,
                                  S40_NEXT_WEAPON_KEY, KEY_USE,    KEY_ESCAPE};
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        event(codes[i], S40_KEY_DOWN, S40_INPUT_GAME);
        expect(1, keys[i]);
        event(codes[i], S40_KEY_UP, S40_INPUT_GAME);
        expect(0, keys[i]);
    }
    event('i', S40_KEY_DOWN, S40_INPUT_TEXT);
    expect(1, 'i');
    event('i', S40_KEY_UP, S40_INPUT_TEXT);
    expect(0, 'i');

    /* Overflow must reconcile a previously delivered hold, not strand it. */
    event(' ', S40_KEY_DOWN, S40_INPUT_GAME);
    expect(1, KEY_FIRE);
    for (unsigned i = 0; i < 70; ++i) {
        event('i', S40_KEY_DOWN, S40_INPUT_GAME);
        event('i', S40_KEY_UP, S40_INPUT_GAME);
    }
    event(' ', S40_KEY_UP, S40_INPUT_GAME);
    expect(0, KEY_FIRE);
    empty();

    /* Keep the existing numeric handset controls in the same binary. */
    s40_input_init(S40_INPUT_KEYPAD);
    event('5', S40_KEY_DOWN, S40_INPUT_GAME);
    expect(1, KEY_FIRE);
    event('5', S40_KEY_UP, S40_INPUT_GAME);
    expect(0, KEY_FIRE);
    empty();
    puts("Doom C3 input contract: PASS");
    return 0;
}
