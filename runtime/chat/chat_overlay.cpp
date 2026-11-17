// The chat over the game: chat_overlay.h. In the touch controls' look: a dark plate with a bronze edge and a gold rule;
// each line on a wine-dark plate with its chat's colour at its left (Global gold, General green, Hub blue, a friend pink).
#include "chat_overlay.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "chat.h"
#include "chat_text.h"
#include "imgui.h"
#include "platform.h"
#include "textinput.h"

namespace chat {
namespace {
OverlayPort g_port;
struct Shown {
    Line line;
    double at;
};
std::vector<Shown> g_shown;
unsigned g_seen_serial;
bool g_serial_known;
// the button: its centre as fractions of the screen, its size (1: its own); a drag under way
constexpr float HOME_X = .11f, HOME_Y = .30f;  // (right of the column of buttons at the left edge, over the stick)
float g_bx = HOME_X, g_by = HOME_Y, g_bs = 1;
bool g_loaded, g_drag, g_dragged, g_on_screen;
float g_drag_dx, g_drag_dy, g_down_x, g_down_y, g_w = 1, g_h = 1;
bool g_panel_open, g_panel_gesture, g_panel_drag, g_list_drag, g_list_tap, g_composer_open, g_bubble_selected, g_panel_bubble_tap;
float g_px = .5f, g_py = .5f, g_ps = 1, g_panel_dx, g_panel_dy, g_list_drag_y, g_list_scroll, g_list_max_scroll;
float g_list_down_x, g_list_down_y;
double g_bubble_used_at;
bool g_wanted_before;
int g_channel = HUB;
std::string g_friend_peer;
char g_draft[1024];
std::string g_error;
struct Box { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
Box g_panel_box, g_title_box, g_close_box, g_list_box, g_compose_box, g_copy_box, g_paste_box, g_add_friend_box, g_send_box, g_friend_back_box, g_channels[4], g_quick[6];
std::vector<Box> g_friend_rows;
std::vector<Box> g_message_rows;
std::vector<unsigned> g_message_serials;
std::string g_selected_message;
std::string g_selected_sender, g_selected_sender_name;
unsigned g_selected_serial;
ImFont *g_compose_font;
float g_compose_font_size, g_compose_text_x;
constexpr double SHOWN = 8;

void load() {
    if (g_loaded) return;
    g_loaded = true;
    float x, y, s;
    if (bubble(&x, &y, &s)) g_bx = std::clamp(x, 0.f, 1.f), g_by = std::clamp(y, 0.f, 1.f), g_bs = std::clamp(s, .6f, 2.f);
    if (panel(&x, &y, &s)) g_px = std::clamp(x, 0.f, 1.f), g_py = std::clamp(y, 0.f, 1.f), g_ps = std::clamp(s, .65f, 1.35f);
}
float radius() { return g_h * .048f * g_bs; }
bool menu_open() { return g_port.menu_open && g_port.menu_open(); }
bool wanted() {  // (the online chat on, or the hub's chat on and a hub up)
#if defined(__ANDROID__) || defined(__APPLE__)
    const bool controls = g_port.controls_shown && g_port.controls_shown();
    return bubble_shown() && (controls || g_composer_open) && !menu_open() &&
           (!textinput_active() || g_composer_open) && (enabled() || (hub_chat() && hub_up()));
#else
    return false;
#endif
}
bool counts(int channel) {
    int o = overlay();
    return o == OVERLAY_ALL || (o == OVERLAY_HUB_FRIENDS && (channel == HUB || channel == FRIEND));
}
ImU32 ink(int c) {
    switch (c) {
    case GLOBAL: return IM_COL32(255, 214, 92, 255);
    case GENERAL: return IM_COL32(150, 214, 110, 255);
    case HUB: return IM_COL32(120, 200, 255, 255);
    default: return IM_COL32(244, 150, 200, 255);
    }
}
std::string mark(int c) { return c == FRIEND ? std::string(t("Friend", "Amigo")) : c == GENERAL ? std::string(t("General", "General")) : channel_name(c); }
ImU32 faded(ImU32 c, float a) { return (c & 0x00ffffffu) | ((ImU32)(((c >> 24) & 0xff) * a) << 24); }
}  // namespace

void overlay_setup(const OverlayPort &port) { g_port = port; }

namespace {
bool inside(const Box &b, float x, float y) { return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1; }
bool channel_ready(int c) {
    if (c == HUB) return hub_chat() && hub_up();
    if (c == FRIEND) {
        if (g_friend_peer.empty() || !enabled()) return false;
        for (const Person &p : people()) if (p.id == g_friend_peer) return p.is_friend && p.mutual && p.online && !p.blocked;
        return false;
    }
    return enabled();
}
bool selected_sender_can_add() {
    std::string key = g_selected_sender;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (key.size() != 64 || key == my_key()) return false;
    if (!std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) return false;
    for (const Person &p : people()) if (p.id == key && (p.is_friend || p.pending || p.request || p.blocked)) return false;
    return true;
}
void stop_composer(bool send) {
    if (!g_composer_open) return;
    textinput_peek(g_draft, sizeof g_draft);
    textinput_end();
    g_composer_open = false;
    if (!send || !g_draft[0]) return;
    std::string why;
    if (!chat::say(g_channel, g_channel == FRIEND ? g_friend_peer : "", g_draft, &why)) g_error = chat::explain(why);
    g_draft[0] = 0;
}
void send_composer() {
    if (!g_composer_open) return;
    textinput_peek(g_draft, sizeof g_draft);
    if (!g_draft[0]) return;
    std::string why;
    if (!chat::say(g_channel, g_channel == FRIEND ? g_friend_peer : "", g_draft, &why)) {
        g_error = chat::explain(why);
        return;
    }
    g_draft[0] = 0;
    textinput_set("");
    textinput_tap();
    g_error.clear();
    g_list_scroll = 0;
}
void poll_composer() {
    if (!g_composer_open) return;
    int chars = 0;
    int event = textinput_poll(g_draft, sizeof g_draft, &chars);
    if (event == TI_ENTER) send_composer();
    // Hiding the system keyboard keeps the draft and composer alive.
}
void begin_composer() {
    if (!channel_ready(g_channel)) return;
    g_draft[0] = 0;
    textinput_begin("", (int)chat::max_text());
    g_composer_open = true;
    g_error.clear();
}
void draw_box(ImDrawList *dl, Box &b, float x, float y, float w, float h, ImU32 color, ImU32 edge, float round, float line) {
    b = {x, y, x + w, y + h};
    dl->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x1, b.y1), color, round);
    dl->AddRect(ImVec2(b.x0, b.y0), ImVec2(b.x1, b.y1), edge, round, 0, line);
}
void draw_unread_badge(ImDrawList *dl, ImFont *font, float unit, float right, float center_y, int count) {
    if (count <= 0) return;
    const std::string label = count > 99 ? "99+" : std::to_string(count);
    const float fs = 11.f * unit;
    const ImVec2 text = font->CalcTextSizeA(fs, FLT_MAX, 0.f, label.c_str());
    const float height = 19.f * unit;
    const float width = std::max(height, text.x + 12.f * unit);
    const float x0 = right - width, y0 = center_y - height * .5f;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(right, y0 + height), IM_COL32(190, 54, 49, 255), height * .5f);
    dl->AddText(font, fs, ImVec2(x0 + (width - text.x) * .5f, y0 + (height - text.y) * .5f), IM_COL32(255, 246, 229, 255), label.c_str());
}
void draw_panel(float w, float h, ImDrawList *dl, ImFont *font) {
    const float unit = std::min(std::max(1.f, h / 720.f) * g_ps, std::min(w * .88f / 760.f, h * .76f / 500.f));
    const float pw = 760.f * unit, ph = 500.f * unit;
    const float x = std::clamp(g_px * w - pw * .5f, 4.f, std::max(4.f, w - pw - 4.f));
    const float y = std::clamp(g_py * h - ph * .5f, 4.f, std::max(4.f, h - ph - 4.f));
    const float pad = 14.f * unit, line = std::max(1.5f, 2.f * unit), fs = 17.f * unit;
    const ImU32 bg = IM_COL32(35, 24, 20, 245), row = IM_COL32(50, 34, 27, 245), edge = IM_COL32(150, 108, 49, 255);
    const ImU32 gold = IM_COL32(242, 198, 78, 255), cream = IM_COL32(244, 232, 200, 255), dim = IM_COL32(173, 157, 133, 255);
    const float head = 50.f * unit, tab_h = 43.f * unit;
    draw_box(dl, g_panel_box, x, y, pw, ph, bg, edge, 8.f * unit, line);
    draw_box(dl, g_title_box, x, y, pw, head, IM_COL32(67, 37, 31, 255), edge, 8.f * unit, line);
    const char *title = t("CO-OP CHAT", "CHAT DE EQUIPO");
    dl->AddText(font, fs * 1.12f, ImVec2(x + pad, y + (head - fs) * .42f), gold, title);
    const float close = 34.f * unit;
    draw_box(dl, g_close_box, x + pw - close - pad * .5f, y + (head - close) * .5f, close, close, row, edge, 5.f * unit, line);
    dl->AddText(font, fs * 1.3f, ImVec2(g_close_box.x0 + close * .34f, g_close_box.y0 + close * .12f), cream, "x");

    const char *tabs[] = {t("Hub", "Sala"), t("General", "General"), t("Global", "Global"), t("Friend", "Amigos")};
    const int channels[] = {HUB, GENERAL, GLOBAL, FRIEND};
    const float gap = 7.f * unit, tw = (pw - 2 * pad - 3 * gap) / 4.f;
    for (int i = 0; i < 4; i++) {
        const float tx = x + pad + i * (tw + gap), ty = y + head + 8.f * unit;
        const bool active = g_channel == channels[i], ready = channel_ready(channels[i]);
        draw_box(dl, g_channels[i], tx, ty, tw, tab_h, active ? IM_COL32(133, 91, 29, 255) : row,
                 active ? gold : edge, 5.f * unit, line);
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, tabs[i]);
        const int unread = chat::unread(channels[i]);
        const float badge_room = unread > 0 ? 27.f * unit : 0.f;
        dl->AddText(font, fs, ImVec2(tx + (tw - badge_room - ts.x) * .5f, ty + (tab_h - ts.y) * .5f), ready ? cream : dim, tabs[i]);
        draw_unread_badge(dl, font, unit, tx + tw - 5.f * unit, ty + tab_h * .5f, unread);
    }
    const float list_y = y + head + tab_h + 18.f * unit, list_h = 156.f * unit;
    draw_box(dl, g_list_box, x + pad, list_y, pw - 2 * pad, list_h, IM_COL32(22, 17, 15, 235), edge, 4.f * unit, line * .7f);
    std::vector<Line> lines;
    std::vector<Person> friends;
    g_friend_rows.clear();
    g_message_rows.clear();
    g_message_serials.clear();
    g_friend_back_box = {};
    if (g_channel == FRIEND && g_friend_peer.empty()) {
        for (const Person &p : people()) if (p.is_friend && !p.blocked) friends.push_back(p);
    } else {
        lines = chat::lines(g_channel, g_channel == FRIEND ? g_friend_peer : "", 60);
        chat::mark_read(g_channel, g_channel == FRIEND ? g_friend_peer : "");
    }
    const float avail = pw - 3 * pad;
    const float inner_top = list_y + 8.f * unit, inner_bottom = list_y + list_h - 8.f * unit;
    const float friend_head_h = g_channel == FRIEND && !g_friend_peer.empty() ? fs + 10.f * unit : 0.f;
    const float scroll_top = inner_top + friend_head_h;
    const float inner_h = inner_bottom - scroll_top;
    std::vector<float> row_heights;
    float content_h = 0;
    row_heights.reserve(lines.size());
    for (const Person &p : friends) {
        const std::string status = p.online ? t("online", "en línea") : t("offline", "desconectado");
        const std::string label = p.name + " · " + status;
        const float friend_text_avail = std::max(1.f, avail - 42.f * unit);
        const ImVec2 ts = font->CalcTextSizeA(fs * .88f, FLT_MAX, friend_text_avail, label.c_str());
        row_heights.push_back(std::max(fs, ts.y) + 5.f * unit);
        content_h += row_heights.back();
    }
    for (const Line &l : lines) {
        const std::string text = l.name + ": " + line_text(l);
        const ImVec2 ts = font->CalcTextSizeA(fs * .88f, FLT_MAX, avail, text.c_str());
        const float rh = std::max(fs, ts.y) + 5.f * unit;
        row_heights.push_back(rh);
        content_h += rh;
    }
    g_list_max_scroll = std::max(0.f, content_h - inner_h);
    g_list_scroll = std::clamp(g_list_scroll, 0.f, g_list_max_scroll);
    float ly = scroll_top - g_list_max_scroll + g_list_scroll;
    dl->PushClipRect(ImVec2(g_list_box.x0 + line, g_list_box.y0 + line), ImVec2(g_list_box.x1 - line, g_list_box.y1 - line), true);
    if (g_channel == FRIEND && g_friend_peer.empty()) {
        if (friends.empty()) dl->AddText(font, fs * .9f, ImVec2(x + pad * 1.5f, ly), dim, t("No friends yet", "Aún no tienes amigos"));
        else for (size_t i = 0; i < friends.size(); i++) {
            const Person &p = friends[i];
            const std::string status = p.online ? t("online", "en línea") : t("offline", "desconectado");
            const std::string label = p.name + " · " + status;
            g_friend_rows.push_back({g_list_box.x0 + 5.f * unit, ly - 3.f * unit,
                                     g_list_box.x1 - 5.f * unit, ly + row_heights[i]});
            dl->AddRectFilled(ImVec2(x + pad, ly), ImVec2(x + pad + 3.f * unit, ly + row_heights[i] - 2.f * unit), ink(FRIEND));
            dl->AddText(font, fs * .88f, ImVec2(x + pad * 1.5f, ly), p.online ? cream : dim, label.c_str(), nullptr, std::max(1.f, avail - 42.f * unit));
            draw_unread_badge(dl, font, unit, g_list_box.x1 - 9.f * unit, ly + row_heights[i] * .5f,
                              chat::unread(FRIEND, p.id));
            ly += row_heights[i];
        }
    } else if (g_channel == FRIEND && !g_friend_peer.empty()) {
        std::string peer_name = person_name(g_friend_peer);
        if (peer_name.empty()) peer_name = t("Friend", "Amigo");
        const std::string back = std::string(t("← PRIVATE CHAT · ", "← CHAT PRIVADO · ")) + peer_name;
        g_friend_back_box = {g_list_box.x0 + 5.f * unit, inner_top - 3.f * unit,
                             g_list_box.x1 - 5.f * unit, inner_top + friend_head_h};
        draw_box(dl, g_friend_back_box, g_friend_back_box.x0, g_friend_back_box.y0,
                 g_friend_back_box.x1 - g_friend_back_box.x0, friend_head_h - 1.f * unit,
                 IM_COL32(78, 39, 52, 245), IM_COL32(222, 119, 164, 255), 4.f * unit, line);
        dl->AddText(font, fs * .78f, ImVec2(g_friend_back_box.x0 + 7.f * unit, inner_top + 2.f * unit),
                    IM_COL32(255, 190, 215, 255), back.c_str(), nullptr, avail - 10.f * unit);
        if (lines.empty()) dl->AddText(font, fs * .9f, ImVec2(x + pad * 1.5f, ly), dim, t("No messages yet", "Sin mensajes"));
        else for (size_t i = 0; i < lines.size(); i++) {
            const Line &l = lines[i];
            const std::string text = l.name + ": " + line_text(l);
            Box message_row{g_list_box.x0 + 5.f * unit, ly - 2.f * unit,
                            g_list_box.x1 - 5.f * unit, ly + row_heights[i] - 2.f * unit};
            g_message_rows.push_back(message_row);
            g_message_serials.push_back(l.serial);
            if (l.serial == g_selected_serial && g_selected_serial)
                dl->AddRectFilled(ImVec2(message_row.x0, message_row.y0), ImVec2(message_row.x1, message_row.y1), faded(gold, .24f), 3.f * unit);
            dl->AddRectFilled(ImVec2(x + pad, ly), ImVec2(x + pad + 3.f * unit, ly + row_heights[i] - 2.f * unit), ink(FRIEND));
            dl->AddText(font, fs * .88f, ImVec2(x + pad * 1.5f, ly), l.mine ? gold : cream, text.c_str(), nullptr, avail);
            ly += row_heights[i];
        }
    } else if (lines.empty()) {
        dl->AddText(font, fs * .9f, ImVec2(x + pad * 1.5f, ly), dim,
                    channel_ready(g_channel) ? t("No messages yet", "Sin mensajes") : t("Channel unavailable", "Canal no disponible"));
    } else {
        for (size_t i = 0; i < lines.size(); i++) {
            const Line &l = lines[i];
            std::string text = l.name + ": " + line_text(l);
            const float row_h = row_heights[i];
            Box message_row{g_list_box.x0 + 5.f * unit, ly - 2.f * unit,
                            g_list_box.x1 - 5.f * unit, ly + row_h - 2.f * unit};
            g_message_rows.push_back(message_row);
            g_message_serials.push_back(l.serial);
            if (l.serial == g_selected_serial && g_selected_serial)
                dl->AddRectFilled(ImVec2(message_row.x0, message_row.y0), ImVec2(message_row.x1, message_row.y1), faded(gold, .24f), 3.f * unit);
            dl->AddRectFilled(ImVec2(x + pad, ly), ImVec2(x + pad + 3.f * unit, ly + row_h - 2.f * unit), ink(g_channel));
            dl->AddText(font, fs * .88f, ImVec2(x + pad * 1.5f, ly), l.mine ? gold : cream, text.c_str(), nullptr, avail);
            ly += row_h;
        }
    }
    if (g_list_max_scroll > 0) {
        const float thumb_h = std::max(10.f * unit, inner_h * inner_h / content_h);
        const float thumb_y = scroll_top + (inner_h - thumb_h) * (1.f - g_list_scroll / g_list_max_scroll);
        dl->AddRectFilled(ImVec2(g_list_box.x1 - 6.f * unit, thumb_y),
                          ImVec2(g_list_box.x1 - 3.f * unit, thumb_y + thumb_h), faded(IM_COL32(242, 198, 78, 210), .75f), 2.f * unit);
    }
    dl->PopClipRect();
    const float qy = list_y + list_h + 12.f * unit, qw = (pw - 2 * pad - 2 * gap) / 3.f, qh = 38.f * unit;
    const std::vector<std::string> quick = chat::quick_phrases();
    for (int i = 0; i < 6; i++) {
        const float qx = x + pad + (i % 3) * (qw + gap), qyy = qy + (i / 3) * (qh + 6.f * unit);
        draw_box(dl, g_quick[i], qx, qyy, qw, qh, row, edge, 4.f * unit, line * .7f);
        const char *label = i < (int)quick.size() ? quick[i].c_str() : "";
        ImVec2 ts = font->CalcTextSizeA(fs * .82f, FLT_MAX, qw - 8.f * unit, label);
        dl->AddText(font, fs * .82f, ImVec2(qx + (qw - ts.x) * .5f, qyy + (qh - ts.y) * .5f),
                    channel_ready(g_channel) ? cream : dim, label, nullptr, qw - 8.f * unit);
    }
    const float cy = qy + 2 * (qh + 6.f * unit) + 10.f * unit, ch = ph - (cy - y) - pad;
    const float send_w = 76.f * unit, paste_w = 62.f * unit, copy_w = 62.f * unit, add_friend_w = 90.f * unit, cg = 6.f * unit;
    const float compose_w = pw - 2 * pad - send_w - paste_w - copy_w - add_friend_w - 4 * cg;
    const bool can_add_friend = selected_sender_can_add();
    draw_box(dl, g_compose_box, x + pad, cy, compose_w, ch, row, edge, 4.f * unit, line * .7f);
    if (g_composer_open) textinput_peek(g_draft, sizeof g_draft);
    const std::string composed = g_composer_open ? (g_draft[0] ? g_draft : t("Type with iOS keyboard…", "Escribe con el teclado de iOS…"))
                                                  : std::string(t("Tap to type with iOS keyboard", "Toca para escribir con el teclado de iOS"));
    g_compose_font = font;
    g_compose_font_size = fs * .84f;
    g_compose_text_x = g_compose_box.x0 + 9.f * unit;
    const float compose_text_y = cy + (ch - fs) * .5f;
    dl->AddText(font, g_compose_font_size, ImVec2(g_compose_text_x, compose_text_y),
                g_composer_open && g_draft[0] ? cream : dim, composed.c_str(), nullptr, compose_w - 18.f * unit);
    if (g_composer_open && g_compose_font && g_draft[0]) {
        const size_t cursor = std::min(textinput_cursor_get(), strlen(g_draft));
        const std::string prefix(g_draft, cursor);
        const float caret_x = g_compose_text_x + g_compose_font->CalcTextSizeA(g_compose_font_size, FLT_MAX, 0.f, prefix.c_str()).x;
        dl->AddLine(ImVec2(caret_x, compose_text_y - 1.f * unit), ImVec2(caret_x, compose_text_y + g_compose_font_size + 1.f * unit), gold, 1.5f * unit);
    }
    draw_box(dl, g_copy_box, g_compose_box.x1 + cg, cy, copy_w, ch,
             g_selected_message.empty() ? IM_COL32(38, 29, 25, 220) : row, edge, 4.f * unit, line * .7f);
    const char *copy = t("Copy", "Copiar");
    ImVec2 cs = font->CalcTextSizeA(fs * .76f, FLT_MAX, 0, copy);
    dl->AddText(font, fs * .76f, ImVec2(g_copy_box.x0 + (copy_w - cs.x) * .5f, cy + (ch - cs.y) * .5f),
                g_selected_message.empty() ? dim : cream, copy);
    draw_box(dl, g_paste_box, g_copy_box.x1 + cg, cy, paste_w, ch, row, edge, 4.f * unit, line * .7f);
    const char *paste = t("Paste", "Pegar");
    ImVec2 ps = font->CalcTextSizeA(fs * .78f, FLT_MAX, 0, paste);
    dl->AddText(font, fs * .78f, ImVec2(g_paste_box.x0 + (paste_w - ps.x) * .5f, cy + (ch - ps.y) * .5f), cream, paste);
    draw_box(dl, g_add_friend_box, g_paste_box.x1 + cg, cy, add_friend_w, ch,
             can_add_friend ? IM_COL32(54, 91, 66, 255) : IM_COL32(38, 29, 25, 220),
             can_add_friend ? IM_COL32(121, 182, 126, 255) : edge, 4.f * unit, line * .7f);
    const char *add_friend = t("Add Friend", "Añadir amigo");
    ImVec2 afs = font->CalcTextSizeA(fs * .70f, add_friend_w - 8.f * unit, 0, add_friend);
    dl->AddText(font, fs * .70f, ImVec2(g_add_friend_box.x0 + (add_friend_w - afs.x) * .5f, cy + (ch - afs.y) * .5f),
                can_add_friend ? cream : dim, add_friend, nullptr, add_friend_w - 8.f * unit);
    draw_box(dl, g_send_box, g_add_friend_box.x1 + cg, cy, send_w, ch, channel_ready(g_channel) ? IM_COL32(119, 82, 27, 255) : row,
             edge, 4.f * unit, line * .7f);
    const char *send = t("Send", "Enviar");
    ImVec2 ss = font->CalcTextSizeA(fs * .9f, FLT_MAX, 0, send);
    dl->AddText(font, fs * .9f, ImVec2(g_send_box.x0 + (send_w - ss.x) * .5f, cy + (ch - ss.y) * .5f),
                channel_ready(g_channel) ? cream : dim, send);
    if (!g_error.empty())
        dl->AddText(font, fs * .7f, ImVec2(x + pad, y + ph - 2.f * unit), IM_COL32(255, 175, 110, 255), g_error.c_str());
}
void quick_send(int i) {
    const std::vector<std::string> messages = chat::quick_phrases();
    std::string why;
    if (i >= 0 && i < (int)messages.size() && !chat::say(g_channel, g_channel == FRIEND ? g_friend_peer : "", messages[i], &why)) g_error = chat::explain(why);
    else g_error.clear();
}
void paste_composer() {
    if (!g_composer_open) begin_composer();
    if (!g_composer_open) return;
#if defined(__APPLE__)
    char clip[2048];
    if (!plat_ios_get_clipboard_text(clip, sizeof clip)) {
        g_error = t("Clipboard is empty or unavailable", "El portapapeles está vacío o no disponible");
        return;
    }
    textinput_type(clip);
#else
    char *clip = SDL_GetClipboardText();
    if (clip && *clip) textinput_type(clip);
    else g_error = t("Clipboard is empty or unavailable", "El portapapeles está vacío o no disponible");
    SDL_free(clip);
#endif
    textinput_peek(g_draft, sizeof g_draft);
    textinput_tap();
    g_error.clear();
}
void copy_selected_message() {
    if (g_selected_message.empty()) return;
#if defined(__APPLE__)
    const bool copied = plat_ios_set_clipboard_text(g_selected_message.c_str());
#else
    const bool copied = SDL_SetClipboardText(g_selected_message.c_str());
#endif
    g_error = copied ? std::string(t("Message copied", "Mensaje copiado"))
                     : std::string(t("Could not copy message", "No se pudo copiar el mensaje"));
}
void add_selected_sender() {
    if (!selected_sender_can_add()) return;
    std::string key = g_selected_sender;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (chat::add_friend_key(key, g_selected_sender_name))
        g_error = t("Friend request sent", "Solicitud de amistad enviada");
    else
        g_error = t("Could not add friend", "No se pudo añadir al amigo");
}

void place_composer_cursor(float x) {
    if (!g_composer_open || !g_compose_font) return;
    textinput_peek(g_draft, sizeof g_draft);
    const size_t len = strlen(g_draft);
    size_t pos = 0;
    float previous = 0.f;
    for (size_t i = 0; i < len;) {
        size_t next = i + 1;
        while (next < len && ((unsigned char)g_draft[next] & 0xC0) == 0x80) next++;
        const std::string prefix(g_draft, next);
        const float current = g_compose_font->CalcTextSizeA(g_compose_font_size, FLT_MAX, 0.f, prefix.c_str()).x;
        if (x < g_compose_text_x + (previous + current) * .5f) { pos = i; break; }
        pos = next;
        previous = current;
        i = next;
    }
    textinput_cursor_set(pos);
}
}  // namespace

bool overlay_frame() {
    if (!active()) {  // (both chats off, as they come: nothing over the game, and the button takes no finger)
        if (g_composer_open) stop_composer(false);
        if (g_serial_known || g_on_screen || g_panel_open) g_shown.clear(), g_on_screen = g_drag = g_panel_open = false, g_serial_known = false;
        return false;
    }
    unsigned last = last_serial();
    if (!g_serial_known) g_seen_serial = last, g_serial_known = true;
    double now = SDL_GetTicks() / 1000.0;
    const bool menu = menu_open();
    if (last != g_seen_serial) {
        for (const Line &l : since(g_seen_serial))
            if (!l.mine && counts(l.channel) && !menu) g_shown.push_back({l, now});
        g_seen_serial = last;
        if (g_shown.size() > 4) g_shown.erase(g_shown.begin(), g_shown.end() - 4);
    }
    g_shown.erase(std::remove_if(g_shown.begin(), g_shown.end(), [now](const Shown &s) { return now - s.at > SHOWN; }), g_shown.end());
    if (menu) g_shown.clear();
    const bool want = wanted();
    if (want && !g_wanted_before) g_bubble_used_at = now;
    g_wanted_before = want;
    if (!want) {
        g_on_screen = false, g_panel_open = false;
        if (g_composer_open) stop_composer(false);
    }  // (gone - the hub left, say - and not drawn)
    poll_composer();
    return !g_shown.empty() || want || g_panel_open;
}

void overlay_draw(float w, float h) {
    g_w = w, g_h = h;
    load();
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImFont *font = ImGui::GetFont();
    const float scale = std::max(1.f, h / 720.f), line = 2 * scale;
    const double now = SDL_GetTicks() / 1000.0;
    const float bubble_alpha = now - g_bubble_used_at >= 5.0 ? .30f : 1.f;
    g_on_screen = wanted();
    const float bx = g_bx * w, by = g_by * h, br = radius();
    const ImU32 plate = IM_COL32(28, 20, 15, 215), edge = IM_COL32(112, 76, 34, 255), rule = IM_COL32(222, 186, 96, 255),
                cream = IM_COL32(244, 232, 200, 255);
    if (g_panel_open) draw_panel(w, h, dl, font);
    if (g_on_screen) {  // (a round plate, a speech bubble in it, the lines not read in a red seal)
        dl->AddCircleFilled(ImVec2(bx, by + line), br, faded(IM_COL32(0, 0, 0, 90), bubble_alpha), 40);
        dl->AddCircleFilled(ImVec2(bx, by), br, faded(g_drag && g_dragged ? IM_COL32(92, 62, 22, 235) : plate, bubble_alpha), 40);
        dl->AddCircle(ImVec2(bx, by), br, faded(edge, bubble_alpha), 40, line * 1.4f);
        dl->AddCircle(ImVec2(bx, by), br - line * 2.2f, faded(rule, bubble_alpha), 40, std::max(1.f, line * .6f));
        if (g_bubble_selected) dl->AddCircle(ImVec2(bx, by), br * 1.28f, faded(IM_COL32(255, 225, 90, 255), bubble_alpha), 40, std::max(2.f, line * 2));
        const float bw = br * .95f, bh = br * .62f;
        const ImVec2 a(bx - bw * .5f, by - bh * .62f), b(bx + bw * .5f, by + bh * .38f);
        dl->AddRectFilled(a, b, faded(cream, bubble_alpha), bh * .35f);
        const ImVec2 tail[3] = {ImVec2(bx - bw * .22f, b.y - 1), ImVec2(bx - bw * .34f, b.y + bh * .38f), ImVec2(bx + bw * .02f, b.y - 1)};
        dl->AddConvexPolyFilled(tail, 3, faded(cream, bubble_alpha));
        for (int i = -1; i <= 1; i++) dl->AddCircleFilled(ImVec2(bx + i * bw * .22f, (a.y + b.y) * .5f), bh * .09f, faded(plate, bubble_alpha), 10);
        const int unread = chat::unread_total();
        if (unread) {
            char n[8];
            snprintf(n, sizeof n, "%d", std::min(unread, 99));
            const float fs = br * .62f, rr = br * .42f;
            const ImVec2 c(bx + br * .72f, by - br * .72f);
            dl->AddCircleFilled(c, rr, faded(IM_COL32(176, 34, 30, 255), bubble_alpha), 24);
            dl->AddCircle(c, rr, faded(rule, bubble_alpha), 24, std::max(1.f, line * .6f));
            const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, n);
            dl->AddText(font, fs, ImVec2(c.x - ts.x * .5f, c.y - ts.y * .5f), faded(cream, bubble_alpha), n);
        }
    }
    if (g_shown.empty()) return;
    // the lines: plates beside the button (or where it would be), the newest at the bottom, fading out
    const float fs = ImGui::GetFontSize() * 1.3f, pad = 7 * scale, wrap = std::min(w * .45f, 640 * scale);
    float x = g_on_screen ? bx + br + 8 * scale : bx, y = by - br;
    if (x + wrap + 2 * pad > w) x = std::max(4.f, bx - br - 8 * scale - wrap - 2 * pad);
    for (const Shown &s : g_shown) {
        const float age = (float)(now - s.at), a = std::clamp(std::min(age / .15f, ((float)SHOWN - age) / .6f), 0.f, 1.f);
        const std::string who = s.line.system ? std::string() : s.line.name + ": ", tag = mark(s.line.channel) + "  ";
        const std::string text = line_text(s.line);
        const ImVec2 ms = font->CalcTextSizeA(fs * .8f, FLT_MAX, 0, tag.c_str()), ws = font->CalcTextSizeA(fs, FLT_MAX, 0, who.c_str());
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, std::max(fs * 4, wrap - ws.x - ms.x), text.c_str());
        const float pw = ms.x + ws.x + ts.x + 2 * pad, ph = std::max(ts.y, fs) + 2 * pad;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + ph), faded(IM_COL32(50, 25, 24, 215), a));
        {  // (the edge as four lines: AddRect's arguments changed order between the ports' Dear ImGui versions)
            const ImU32 e = faded(edge, a);
            const float t = line * .7f;
            dl->AddLine(ImVec2(x, y), ImVec2(x + pw, y), e, t), dl->AddLine(ImVec2(x + pw, y), ImVec2(x + pw, y + ph), e, t);
            dl->AddLine(ImVec2(x + pw, y + ph), ImVec2(x, y + ph), e, t), dl->AddLine(ImVec2(x, y + ph), ImVec2(x, y), e, t);
        }
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + line * 1.5f, y + ph), faded(ink(s.line.channel), a));
        float tx = x + pad;
        dl->AddText(font, fs * .8f, ImVec2(tx, y + pad + fs * .12f), faded(ink(s.line.channel), a), tag.c_str());
        tx += ms.x;
        if (!who.empty()) dl->AddText(font, fs, ImVec2(tx, y + pad), faded(IM_COL32(255, 214, 92, 255), a), who.c_str());
        dl->AddText(font, fs, ImVec2(tx + ws.x, y + pad), faded(cream, a), text.c_str(), nullptr, std::max(fs * 4, wrap - ws.x - ms.x));
        y += ph + 3 * scale;
    }
}

bool bubble_down(float fx, float fy) {
    const float x = fx * g_w, y = fy * g_h;
    if (g_panel_open) {
        if (g_on_screen && std::hypot(x - g_bx * g_w, y - g_by * g_h) <= radius() * 1.15f) {
            g_bubble_used_at = SDL_GetTicks() / 1000.0;
            g_panel_bubble_tap = true;
            g_panel_gesture = true;
            return true;
        }
        g_panel_bubble_tap = false;
        if (inside(g_panel_box, x, y)) g_bubble_used_at = SDL_GetTicks() / 1000.0;
        g_panel_gesture = true;
        if (!inside(g_panel_box, x, y)) { g_panel_drag = false; return true; }
        if (inside(g_list_box, x, y)) {
            g_list_drag = true;
            g_list_tap = true;
            g_list_drag_y = y;
            g_list_down_x = x, g_list_down_y = y;
            return true;
        }
        g_panel_drag = inside(g_title_box, x, y) && !inside(g_close_box, x, y);
        if (g_panel_drag) g_panel_dx = g_px * g_w - x, g_panel_dy = g_py * g_h - y;
        return true;
    }
    if (!g_on_screen) return false;
    g_panel_gesture = false;
    if (std::hypot(x - g_bx * g_w, y - g_by * g_h) > radius() * 1.15f) return false;
    g_bubble_used_at = SDL_GetTicks() / 1000.0;
    g_drag = true, g_dragged = false;
    g_down_x = x, g_down_y = y, g_drag_dx = g_bx * g_w - x, g_drag_dy = g_by * g_h - y;
    return true;
}
void bubble_finger(bool up, float fx, float fy) {
    if (g_panel_gesture) {
        const float x = fx * g_w, y = fy * g_h;
        if (!up) g_bubble_used_at = SDL_GetTicks() / 1000.0;
        if (g_panel_bubble_tap) {
            if (up) {
                // Treat tapping the bubble while the panel is open exactly like its close button.
                // In particular, end system text input so touch controls become active again.
                stop_composer(false);
                g_panel_open = g_panel_gesture = g_panel_bubble_tap = false;
            }
            return;
        }
        if (g_list_drag) {
            if (!up) {
                if (std::hypot(x - g_list_down_x, y - g_list_down_y) > 8.f) g_list_tap = false;
                g_list_scroll = std::clamp(g_list_scroll + y - g_list_drag_y, 0.f, g_list_max_scroll);
                g_list_drag_y = y;
            } else {
                if (g_list_tap && g_channel == FRIEND && g_friend_peer.empty()) {
                    for (size_t i = 0; i < g_friend_rows.size(); i++) if (inside(g_friend_rows[i], x, y)) {
                        std::vector<Person> friends = people();
                        size_t index = 0;
                        for (const Person &p : friends) if (p.is_friend && !p.blocked) {
                            if (index++ == i) {
                                g_friend_peer = p.id;
                                g_selected_serial = 0;
                                g_selected_message.clear();
                                g_selected_sender.clear();
                                g_selected_sender_name.clear();
                                break;
                            }
                        }
                        g_list_scroll = 0;
                        break;
                    }
                } else if (g_list_tap) {
                    if (g_channel == FRIEND && !g_friend_peer.empty() && inside(g_friend_back_box, x, y)) {
                        g_friend_peer.clear();
                        g_list_scroll = 0;
                        g_selected_serial = 0;
                        g_selected_message.clear();
                        g_selected_sender.clear();
                        g_selected_sender_name.clear();
                    } else for (size_t i = 0; i < g_message_rows.size(); i++) if (inside(g_message_rows[i], x, y)) {
                        std::vector<Line> messages = chat::lines(g_channel, g_channel == FRIEND ? g_friend_peer : "", 60);
                        for (const Line &message : messages) if (message.serial == g_message_serials[i]) {
                            g_selected_serial = message.serial;
                            g_selected_message = line_text(message);
                            g_selected_sender = message.mine ? std::string() : message.from;
                            g_selected_sender_name = message.name;
                            g_error.clear();
                            break;
                        }
                        break;
                    }
                }
                g_list_tap = false;
                g_list_drag = g_panel_gesture = false;
            }
            return;
        }
        if (g_panel_drag && !up) {
            const float pw = g_panel_box.x1 - g_panel_box.x0, ph = g_panel_box.y1 - g_panel_box.y0;
            g_px = std::clamp((x + g_panel_dx) / g_w, pw * .5f / g_w, 1.f - pw * .5f / g_w);
            g_py = std::clamp((y + g_panel_dy) / g_h, ph * .5f / g_h, 1.f - ph * .5f / g_h);
        }
        if (up) {
            if (g_panel_drag) set_panel(g_px, g_py, g_ps);
            else if (inside(g_panel_box, x, y)) {
                if (inside(g_close_box, x, y)) {
                    stop_composer(false), g_panel_open = false;
                } else if (inside(g_compose_box, x, y)) {
                    if (!g_composer_open) begin_composer();
                    else place_composer_cursor(x), textinput_tap();
                } else if (inside(g_send_box, x, y)) {
                    send_composer();
                } else if (inside(g_paste_box, x, y)) {
                    paste_composer();
                } else if (inside(g_copy_box, x, y)) {
                    copy_selected_message();
                } else if (inside(g_add_friend_box, x, y)) {
                    add_selected_sender();
                } else {
                    for (int i = 0; i < 4; i++) if (inside(g_channels[i], x, y)) {
                        g_channel = i == 0 ? HUB : i == 1 ? GENERAL : i == 2 ? GLOBAL : FRIEND;
                        g_error.clear(), g_list_scroll = 0, g_selected_serial = 0, g_selected_message.clear(),
                            g_selected_sender.clear(), g_selected_sender_name.clear();
                    }
                    for (int i = 0; i < 6; i++) if (inside(g_quick[i], x, y) && channel_ready(g_channel)) quick_send(i);
                }
            } else if (!g_composer_open) {
                stop_composer(false), g_panel_open = false;
            }
            g_panel_gesture = g_panel_drag = false;
        }
        return;
    }
    if (!g_drag) return;
    const float x = fx * g_w, y = fy * g_h;
    if (std::hypot(x - g_down_x, y - g_down_y) > g_h * .02f) g_dragged = true;
    g_bubble_used_at = SDL_GetTicks() / 1000.0;
    if (g_dragged) {
        const float r = radius();
        g_bx = std::clamp((x + g_drag_dx) / g_w, r / g_w, 1 - r / g_w);
        g_by = std::clamp((y + g_drag_dy) / g_h, r / g_h, 1 - r / g_h);
    }
    if (!up) return;
    g_drag = false;
    if (g_dragged) {
        set_bubble(g_bx, g_by, g_bs);
        return;
    }
    g_panel_open = true;
    if (g_composer_open) textinput_tap();
    if (g_channel == HUB && !channel_ready(HUB)) g_channel = enabled() ? GLOBAL : GENERAL;
}
void bubble_size(float s) {
    load();
    g_bs = std::clamp(s, .6f, 2.f);
    set_bubble(g_bx, g_by, g_bs);
}
float bubble_scale() {
    load();
    return g_bs;
}
void bubble_reset() {
    load();
    g_bx = HOME_X, g_by = HOME_Y, g_bs = 1;
    set_bubble(g_bx, g_by, g_bs);
}
void bubble_place(float *x, float *y, float *size) {
    load();
    if (x) *x = g_bx;
    if (y) *y = g_by;
    if (size) *size = g_bs;
}
void bubble_preview(float x, float y, float size) {
    load();
    g_bs = std::clamp(size, .6f, 2.f);
    const float r = radius();
    g_bx = g_w > 0 ? std::clamp(x, r / g_w, 1.f - r / g_w) : std::clamp(x, 0.f, 1.f);
    g_by = g_h > 0 ? std::clamp(y, r / g_h, 1.f - r / g_h) : std::clamp(y, 0.f, 1.f);
}
void bubble_commit() { set_bubble(g_bx, g_by, g_bs); }
void bubble_edit_selected(bool selected) { g_bubble_selected = selected; }
void panel_preview_size(float s) {
    load();
    g_ps = std::clamp(s, .65f, 1.35f);
}
void panel_size(float s) {
    panel_preview_size(s);
    set_panel(g_px, g_py, g_ps);
}
float panel_scale() {
    load();
    return g_ps;
}
void panel_reset() {
    load();
    g_px = .5f, g_py = .5f, g_ps = 1.f;
    set_panel(g_px, g_py, g_ps);
}

}  // namespace chat
