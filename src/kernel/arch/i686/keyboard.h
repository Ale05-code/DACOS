#pragma once
#include <stdbool.h>

// Key codes returned by Keyboard_GetKey():
//  - 1..255: a character (code page 437, the VGA font: e.g. 0x8A = 'è')
//    '\n' = Enter, '\b' = Backspace, '\t' = Tab, 27 = Esc
//  - Ctrl + letter gives 1..26 (Ctrl+A = 1, Ctrl+S = 19, ...), like in a terminal
//  - special keys: the KEY_* values below
enum {
    KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
    KEY_INSERT, KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
};

#define KEY_CTRL(letter)    ((letter) - 'a' + 1)
#define KEY_ESCAPE          27

typedef enum {
    KEYMAP_IT,
    KEYMAP_US,
} Keymap;

void Keyboard_Initialize();
int Keyboard_GetKey();              // waits for a key
bool Keyboard_HasKey();
void Keyboard_SetKeymap(Keymap keymap);
Keymap Keyboard_GetKeymap();
