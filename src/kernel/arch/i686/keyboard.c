#include "keyboard.h"
#include "io.h"
#include "irq.h"
#include <stdint.h>

// PS/2 keyboard driver.
// When a key is pressed or released the keyboard controller raises IRQ 1 and
// puts a "scancode" on port 0x60. We use scancode set 1 (the BIOS default):
//  - press = code, release = code | 0x80
//  - some keys (arrows, right Ctrl, AltGr, ...) are preceded by the byte 0xE0

#define KBD_DATA_PORT       0x60
#define KBD_STATUS_PORT     0x64

#define BUFFER_SIZE         64

static volatile int g_Buffer[BUFFER_SIZE];
static volatile int g_BufferHead = 0, g_BufferTail = 0;

static bool g_Extended = false;
static bool g_LeftShift = false, g_RightShift = false;
static bool g_Ctrl = false, g_Alt = false, g_AltGr = false;
static bool g_CapsLock = false, g_NumLock = true;
static Keymap g_Keymap = KEYMAP_IT;

// --- layouts: index = scancode (set 1), 0 = no character ---------------------

#define SC_COUNT 0x59

static const uint8_t g_US_Normal[SC_COUNT] = {
    [0x01] = 27,
    [0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    [0x0F] = '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    [0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    [0x2B] = '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+', [0x56] = '\\',
};

static const uint8_t g_US_Shift[SC_COUNT] = {
    [0x01] = 27,
    [0x02] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    [0x0F] = '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    [0x1E] = 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    [0x2B] = '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+', [0x56] = '|',
};

// Italian layout. Accented letters use their code page 437 value.
#define CP_E_GRAVE      0x8A    // è
#define CP_E_ACUTE      0x82    // é
#define CP_A_GRAVE      0x85    // à
#define CP_O_GRAVE      0x95    // ò
#define CP_U_GRAVE      0x97    // ù
#define CP_I_GRAVE      0x8D    // ì
#define CP_C_CEDILLA    0x87    // ç
#define CP_DEGREE       0xF8    // °
#define CP_POUND        0x9C    // £
#define CP_SECTION      0x15    // §

static const uint8_t g_IT_Normal[SC_COUNT] = {
    [0x01] = 27,
    [0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\'', CP_I_GRAVE, '\b',
    [0x0F] = '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', CP_E_GRAVE, '+', '\n',
    [0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', CP_O_GRAVE, CP_A_GRAVE, '\\',
    [0x2B] = CP_U_GRAVE, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-',
    [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+', [0x56] = '<',
};

static const uint8_t g_IT_Shift[SC_COUNT] = {
    [0x01] = 27,
    [0x02] = '!', '"', CP_POUND, '$', '%', '&', '/', '(', ')', '=', '?', '^', '\b',
    [0x0F] = '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', CP_E_ACUTE, '*', '\n',
    [0x1E] = 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', CP_C_CEDILLA, CP_DEGREE, '|',
    [0x2B] = CP_SECTION, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', ';', ':', '_',
    [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+', [0x56] = '>',
};

// AltGr (right Alt), as in the Linux "it" layout
static const uint8_t g_IT_AltGr[SC_COUNT] = {
    [0x08] = '{', [0x09] = '[', [0x0A] = ']', [0x0B] = '}',
    [0x0C] = '`', [0x0D] = '~',
    [0x1A] = '[', [0x1B] = ']',
    [0x27] = '@', [0x28] = '#',
};

static const uint8_t g_IT_ShiftAltGr[SC_COUNT] = {
    [0x1A] = '{', [0x1B] = '}',
};

// keypad 7 8 9 - 4 5 6 + 1 2 3 0 . (scancodes 0x47..0x53)
static const char g_KeypadDigits[] = "789-456+1230.";
static const int g_KeypadNavigation[] = {
    KEY_HOME, KEY_UP, KEY_PAGE_UP, '-', KEY_LEFT, 0, KEY_RIGHT, '+',
    KEY_END, KEY_DOWN, KEY_PAGE_DOWN, KEY_INSERT, KEY_DELETE
};

// ------------------------------------------------------------------------------

static void Keyboard_Push(int key)
{
    int next = (g_BufferHead + 1) % BUFFER_SIZE;
    if (next == g_BufferTail)
        return;                     // buffer full: drop the key
    g_Buffer[g_BufferHead] = key;
    g_BufferHead = next;
}

static bool IsLetter(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int TranslateExtended(uint8_t code)
{
    switch (code)
    {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x49: return KEY_PAGE_UP;
        case 0x51: return KEY_PAGE_DOWN;
        case 0x52: return KEY_INSERT;
        case 0x53: return KEY_DELETE;
        case 0x1C: return '\n';     // keypad Enter
        case 0x35: return '/';      // keypad /
        default:   return 0;
    }
}

static int Translate(uint8_t code)
{
    bool shift = g_LeftShift || g_RightShift;

    // function keys
    if (code >= 0x3B && code <= 0x44)
        return KEY_F1 + (code - 0x3B);
    if (code == 0x57) return KEY_F11;
    if (code == 0x58) return KEY_F12;

    // keypad (without the 0xE0 prefix)
    if (code >= 0x47 && code <= 0x53)
    {
        int index = code - 0x47;
        if (g_KeypadDigits[index] == '-' || g_KeypadDigits[index] == '+')
            return g_KeypadDigits[index];
        return g_NumLock ? g_KeypadDigits[index] : g_KeypadNavigation[index];
    }

    if (code >= SC_COUNT)
        return 0;

    uint8_t c = 0;
    if (g_Keymap == KEYMAP_IT && g_AltGr)
    {
        c = shift ? g_IT_ShiftAltGr[code] : g_IT_AltGr[code];
        if (c == 0)
            c = g_IT_AltGr[code];
        return c;
    }

    const uint8_t* normal = (g_Keymap == KEYMAP_IT) ? g_IT_Normal : g_US_Normal;
    const uint8_t* shifted = (g_Keymap == KEYMAP_IT) ? g_IT_Shift : g_US_Shift;

    c = normal[code];

    // Ctrl + letter → 1..26
    if (g_Ctrl && IsLetter(c))
        return c - 'a' + 1;

    // Caps Lock inverts Shift, but only for letters
    bool upper = shift;
    if (g_CapsLock && IsLetter(c))
        upper = !upper;

    return upper ? shifted[code] : c;
}

static void Keyboard_IRQHandler(Registers* regs)
{
    uint8_t scancode = i686_inb(KBD_DATA_PORT);

    if (scancode == 0xE0)
    {
        g_Extended = true;
        return;
    }

    bool released = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;
    bool extended = g_Extended;
    g_Extended = false;

    // modifier keys: remember if they are held down
    switch (code)
    {
        case 0x2A: if (!extended) g_LeftShift = !released;  return;   // (E0 2A = part of Print Screen)
        case 0x36: g_RightShift = !released;                return;
        case 0x1D: g_Ctrl = !released;                      return;   // left or right Ctrl
        case 0x38:
            if (extended) g_AltGr = !released;              // right Alt = AltGr
            else g_Alt = !released;
            return;
    }

    if (released)
        return;

    if (code == 0x3A) { g_CapsLock = !g_CapsLock; return; }
    if (code == 0x45) { g_NumLock = !g_NumLock; return; }
    if (code == 0x46) return;                           // Scroll Lock

    int key = extended ? TranslateExtended(code) : Translate(code);
    if (key != 0)
        Keyboard_Push(key);
}

void Keyboard_Initialize()
{
    // throw away anything still waiting in the controller
    while (i686_inb(KBD_STATUS_PORT) & 1)
        i686_inb(KBD_DATA_PORT);

    i686_IRQ_RegisterHandler(1, Keyboard_IRQHandler);
    i686_IRQ_Unmask(1);
}

bool Keyboard_HasKey()
{
    return g_BufferHead != g_BufferTail;
}

int Keyboard_GetKey()
{
    while (!Keyboard_HasKey())
        i686_Halt();                // sleep until the next interrupt

    int key = g_Buffer[g_BufferTail];
    g_BufferTail = (g_BufferTail + 1) % BUFFER_SIZE;
    return key;
}

void Keyboard_SetKeymap(Keymap keymap)
{
    g_Keymap = keymap;
}

Keymap Keyboard_GetKeymap()
{
    return g_Keymap;
}
