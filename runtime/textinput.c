// Typed text for the game's text entries. On the Switch a text entry brings up the system keyboard (on screen, or a
// USB keyboard). Here, while the game asks for text, the physical keyboard types: characters go into the entry (the
// game shows them in its own text box as they change), Backspace deletes, Ctrl+V pastes, Enter confirms, Escape
// cancels. Meanwhile the keys don't act as controller buttons (L = A, WASD...): runtime/host_input.c asks
// textinput_event first. A controller can still confirm the entry as it is with Plus (Start), or cancel with B.
#include <pthread.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "overlay.h"
#include "rt.h"
#include "textinput.h"

#define TEXT_MAX 0x7d0  // bytes (the Switch keyboard's own limit is 500 characters)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_active;
static bool g_keyboard_suspended;
static int g_window_on;  // text input on: 0 no, 1 for an entry, 2 for the port's menu (no screen keyboard), 3 for the
                         // port's menu with the phone's keyboard (its keyboard view asked for it: textinput_menu_keyboard),
                         // 4 for a field of Forge's menu with the phone's keyboard (overlay_forge_typing)
// The port's menu (its thread): wants the phone's keyboard; its key's presses, counted (and as far as the window went:
// the main thread's); Back put the keyboard away, not told to the menu yet (meanwhile what it wants is not asked again;
// told, the menu stops wanting it until its key is pressed again)
static int g_menu_keyboard, g_menu_show, g_menu_closed;
#ifdef __ANDROID__
static int g_menu_shown;
#endif
static char g_text[TEXT_MAX + 4];
static int g_max_chars, g_event;  // g_event: TI_* waiting for the game's next poll (ENTER / CANCEL win over CHANGED)
static size_t g_cursor;            // UTF-8 byte offset of the insertion point

static int utf8_chars(const char *s) {
    int n = 0;
    for (; *s; s++) n += ((unsigned char)*s & 0xC0) != 0x80;
    return n;
}

static size_t utf8_prev(const char *s, size_t pos) {
    if (!pos) return 0;
    pos--;
    while (pos && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

static size_t utf8_next(const char *s, size_t pos) {
    size_t len = strlen(s);
    if (pos >= len) return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

static void post(int ev) {
    if (ev > g_event) g_event = ev;
}

void textinput_begin(const char *initial, int max_chars) {
    pthread_mutex_lock(&g_lock);
    g_active = true;
    g_max_chars = max_chars;
    g_event = TI_NONE;
    snprintf(g_text, sizeof g_text, "%s", initial ? initial : "");
    g_cursor = strlen(g_text);
    pthread_mutex_unlock(&g_lock);
    LOG("[textinput] text entry (up to %d characters, \"%s\"): type, Enter confirms, Escape cancels\n", max_chars,
        initial ? initial : "");
}

void textinput_end(void) {
    pthread_mutex_lock(&g_lock);
    g_active = false;
    g_event = TI_NONE;
    __atomic_store_n(&g_keyboard_suspended, false, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_lock);
}

int textinput_poll(char *out, size_t size, int *chars) {
    pthread_mutex_lock(&g_lock);
    int ev = g_event;
    g_event = TI_NONE;
    snprintf(out, size, "%s", g_text);
    if (chars) *chars = utf8_chars(g_text);
    pthread_mutex_unlock(&g_lock);
    return ev;
}

void textinput_peek(char *out, size_t size) {
    pthread_mutex_lock(&g_lock);
    snprintf(out, size, "%s", g_text);
    pthread_mutex_unlock(&g_lock);
}

void textinput_cursor_set(size_t byte_offset) {
    pthread_mutex_lock(&g_lock);
    size_t len = strlen(g_text);
    if (byte_offset > len) byte_offset = len;
    while (byte_offset && byte_offset < len && ((unsigned char)g_text[byte_offset] & 0xC0) == 0x80) byte_offset--;
    g_cursor = byte_offset;
    pthread_mutex_unlock(&g_lock);
}

size_t textinput_cursor_get(void) {
    pthread_mutex_lock(&g_lock);
    size_t pos = g_cursor;
    pthread_mutex_unlock(&g_lock);
    return pos;
}

void textinput_cursor_move(int direction) {
    pthread_mutex_lock(&g_lock);
    size_t len = strlen(g_text);
    if (direction == -2) g_cursor = 0;
    else if (direction == 2) g_cursor = len;
    else if (direction < 0) g_cursor = utf8_prev(g_text, g_cursor);
    else if (direction > 0) g_cursor = utf8_next(g_text, g_cursor);
    pthread_mutex_unlock(&g_lock);
}

bool textinput_active(void) { return __atomic_load_n(&g_active, __ATOMIC_RELAXED); }

void textinput_menu_keyboard(bool want) { __atomic_store_n(&g_menu_keyboard, want ? 1 : 0, __ATOMIC_RELAXED); }
void textinput_menu_keyboard_show(void) { __atomic_add_fetch(&g_menu_show, 1, __ATOMIC_RELAXED); }
bool textinput_menu_keyboard_closed(void) {
    if (!__atomic_load_n(&g_menu_closed, __ATOMIC_ACQUIRE)) return false;
    __atomic_store_n(&g_menu_keyboard, 0, __ATOMIC_RELEASE);  // (first: the window doesn't bring it up again meanwhile)
    __atomic_store_n(&g_menu_closed, 0, __ATOMIC_RELEASE);
    return true;
}

void textinput_type(const char *s) {
    pthread_mutex_lock(&g_lock);
    if (g_active) {
        size_t len = strlen(g_text);
        int chars = utf8_chars(g_text);
        bool added = false;
        while (*s) {
            size_t n = 1;  // one character (its UTF-8 bytes)
            while (s[n] && ((unsigned char)s[n] & 0xC0) == 0x80) n++;
            bool control = (unsigned char)*s < 0x20 || *s == 0x7f;  // (pasted line breaks, tabs)
            if (!control) {
                if ((g_max_chars > 0 && chars >= g_max_chars) || len + n > TEXT_MAX) break;
                memmove(g_text + g_cursor + n, g_text + g_cursor, len - g_cursor + 1);
                memcpy(g_text + g_cursor, s, n);
                g_cursor += n;
                len += n;
                chars++;
                added = true;
            }
            s += n;
        }
        if (added) post(TI_CHANGED);
    }
    pthread_mutex_unlock(&g_lock);
}

void textinput_set(const char *text) {
    pthread_mutex_lock(&g_lock);
    if (g_active) {
        g_text[0] = 0;
        g_cursor = 0;
        pthread_mutex_unlock(&g_lock);
        textinput_type(text);  // (the entry's limit and the control characters as typed)
        pthread_mutex_lock(&g_lock);
        post(TI_CHANGED);
    }
    pthread_mutex_unlock(&g_lock);
}

void textinput_key(int what) {
    pthread_mutex_lock(&g_lock);
    if (g_active) {
        if (what == TI_NONE) {  // Backspace: the last character
            if (g_cursor) {
                size_t prev = utf8_prev(g_text, g_cursor);
                memmove(g_text + prev, g_text + g_cursor, strlen(g_text + g_cursor) + 1);
                g_cursor = prev;
                post(TI_CHANGED);
            }
        } else {
            post(what);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

bool textinput_event(const SDL_Event *e) {
    if (!textinput_active()) return false;
    switch (e->type) {
    case SDL_EVENT_TEXT_INPUT:
        textinput_type(e->text.text);
        return true;
    case SDL_EVENT_KEY_DOWN: {
        SDL_Scancode sc = e->key.scancode;
        if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return false;  // (screenshots, MSAA...)
        if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) && (e->key.mod & SDL_KMOD_ALT)) return false;
        if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) && !e->key.repeat) textinput_key(TI_ENTER);
        else if ((sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_AC_BACK) && !e->key.repeat) textinput_key(TI_CANCEL);
        else if (sc == SDL_SCANCODE_BACKSPACE) textinput_key(TI_NONE);
        else if (sc == SDL_SCANCODE_DELETE) {
            pthread_mutex_lock(&g_lock);
            size_t next = utf8_next(g_text, g_cursor);
            if (next > g_cursor) {
                memmove(g_text + g_cursor, g_text + next, strlen(g_text + next) + 1);
                post(TI_CHANGED);
            }
            pthread_mutex_unlock(&g_lock);
        }
        else if (sc == SDL_SCANCODE_LEFT) textinput_cursor_move(-1);
        else if (sc == SDL_SCANCODE_RIGHT) textinput_cursor_move(1);
        else if (sc == SDL_SCANCODE_HOME) textinput_cursor_move(-2);
        else if (sc == SDL_SCANCODE_END) textinput_cursor_move(2);
        else if (sc == SDL_SCANCODE_V && (e->key.mod & SDL_KMOD_CTRL)) {
            char *clip = SDL_GetClipboardText();
            if (clip) textinput_type(clip);
            SDL_free(clip);
        }
        return true;
    }
    case SDL_EVENT_KEY_UP:
        return true;
    }
    return false;
}

#ifdef __ANDROID__
// The phone's keyboard for the port's menu: a server's name or a key, as typed (no capitals, no corrections)
static void start_menu_keyboard(SDL_Window *w) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
    SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    SDL_StartTextInputWithProperties(w, props);
    SDL_DestroyProperties(props);
}
#endif

void textinput_update(void) {
    int want = textinput_active() && !__atomic_load_n(&g_keyboard_suspended, __ATOMIC_ACQUIRE) ? 1 : overlay_menu_open() ? 2 : 0;  // (Forge's menu takes typed text too)
#if defined(__APPLE__)
    // The settings panel has its own keyboard view on iOS. SDL text input for the open menu alone
    // raises the system keyboard, so reserve that keyboard for an active game/chat text entry.
    if (want == 2 && SDL_GetPlatform() && !strcmp(SDL_GetPlatform(), "iOS")) want = 0;
#endif
#ifdef __ANDROID__
    int show = __atomic_load_n(&g_menu_show, __ATOMIC_RELAXED);
    // (a field of Forge's menu takes text: the phone's keyboard, as for the port's menu - 4, its own state - until the
    // field lets go or Back puts the keyboard away)
    static bool forge_away;
    bool forge_typing = want == 2 && overlay_forge_typing();
    if (!forge_typing) forge_away = false;
    if (forge_typing && !forge_away) want = 4;
    if (want == 4 && g_window_on == 4) {
        SDL_Window *w = ui_window();
        if (w && !SDL_TextInputActive(w)) {  // (Back put it away: SDL turned text input off)
            forge_away = true;
            g_window_on = 0;
            want = 2;
        }
    }
    // (Back's mark read before what the menu wants: the menu clears what it wants before the mark, so one taking the mark
    // meanwhile never brings the keyboard up again for a pass - textinput_menu_keyboard_closed)
    if (want == 2 && !__atomic_load_n(&g_menu_closed, __ATOMIC_ACQUIRE) && __atomic_load_n(&g_menu_keyboard, __ATOMIC_RELAXED))
        want = 3;
    // (the menu's: Back put the phone's keyboard away and SDL turned text input off by itself - the menu keeps its view and
    // its text, a physical keyboard types into it again, and its key brings the phone's keyboard back; or the keyboard's
    // own button hid it, and the menu's key asks for it again)
    if (want == 3 && g_window_on == 3) {
        SDL_Window *w = ui_window();
        if (w && !SDL_TextInputActive(w)) {
            __atomic_store_n(&g_menu_closed, 1, __ATOMIC_RELAXED);
            LOG("[textinput] the phone's keyboard put away (Back): the menu's typing stays\n");
            g_window_on = 0;
            want = 2;
        } else if (w && show != g_menu_shown) {  // (SDL can't tell that the keyboard's own button hid it)
            SDL_StopTextInput(w);
            start_menu_keyboard(w);
        }
    }
    g_menu_shown = show;
    // (Back hid the screen keyboard and SDL turned text input off by itself: on again for a physical keyboard, the
    // screen keyboard still hidden; a tap brings it back, textinput_tap)
    if (want == 1 && g_window_on == 1) {
        SDL_Window *w = ui_window();
        if (w && !SDL_TextInputActive(w) && !SDL_ScreenKeyboardShown(w)) {
            SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
            SDL_StartTextInput(w);
            SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
        }
    }
#endif
    if (want == g_window_on) return;
    SDL_Window *w = ui_window();
    if (!w) return;
    if (g_window_on) SDL_StopTextInput(w);
#ifdef __ANDROID__
    // (there text input shows the screen keyboard: for the game's entries; the port's menu has its own keyboard view,
    // a physical keyboard still types into it, and the phone's keyboard only when that view's key asks for it)
    if (want) SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, want == 2 ? "0" : "1");
    if (want == 3 || want == 4) start_menu_keyboard(w);
    else
#endif
    if (want) SDL_StartTextInput(w);
    g_window_on = want;
}

// A tap while an entry is open brings the system keyboard back if the player had hidden it.
void textinput_tap(void) {
    __atomic_store_n(&g_keyboard_suspended, false, __ATOMIC_RELEASE);
    textinput_update();
#ifdef __ANDROID__
    SDL_Window *w = ui_window();
    if (g_window_on == 1 && w && !SDL_ScreenKeyboardShown(w)) {
        SDL_StopTextInput(w);
        SDL_StartTextInput(w);
    }
#elif defined(__APPLE__)
    SDL_Window *w = ui_window();
    if (textinput_active() && w && !SDL_ScreenKeyboardShown(w)) {
        SDL_StopTextInput(w);
        SDL_StartTextInput(w);
    }
#endif
}

void textinput_keyboard_hide(void) {
    __atomic_store_n(&g_keyboard_suspended, true, __ATOMIC_RELEASE);
    textinput_update();
}
