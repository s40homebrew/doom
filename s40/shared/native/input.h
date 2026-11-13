/* SPDX-License-Identifier: MIT */
/* Native event translation, independent of firmware addresses and game state. */
#ifndef S40_INPUT_H
#define S40_INPUT_H

#include <stdint.h>

#include "s40/events.h"

enum s40_input_context { S40_INPUT_GAME, S40_INPUT_MENU, S40_INPUT_TEXT };

/* Use Doom's existing weapon-cycle responder, which retains a tap until the
 * next game tic. A synthetic number-key down/up pair can disappear before it.
 */
#define S40_NEXT_WEAPON_KEY 'n'
#define S40_PREVIOUS_WEAPON_KEY 'b'

void s40_input_init(unsigned layout);
void s40_input_event(uint32_t value, int type, enum s40_input_context context);
int s40_input_next(int* pressed, unsigned char* key);

#endif
