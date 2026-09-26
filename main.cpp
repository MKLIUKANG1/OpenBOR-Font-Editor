// main.cpp — OpenBOR Font Editor
// Defaults: charW=8 charH=8 first=0 spacing=0 scale=3
// Прозрачность: только магента (r>200, b>200, g<80)
// Окно Help/About по клавише H
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <png.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

static std::ofstream g_log;
static void log_init() {
    g_log.open("debug.log", std::ios::app);
    if (g_log) { std::time_t t = std::time(nullptr); g_log << "\n=== Session " << std::ctime(&t); }
}
static void log_msg(const std::string& s) {
    if (g_log) { g_log << s << "\n"; g_log.flush(); }
    SDL_Log("%s", s.c_str());
}

// ==================================================================
//                     МАГИЧЕСКИЙ ЦВЕТ (как в первой версии)
// ==================================================================
static bool isMagicPixel(Uint8 r, Uint8 g, Uint8 b) {
    return r > 200 && b > 200 && g < 80;
}

static SDL_Surface* makeTransparentSurface(SDL_Surface* src) {
    if (!src) return nullptr;
    SDL_Surface* dst = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!dst) return nullptr;
    SDL_LockSurface(dst);
    Uint32* px = (Uint32*)dst->pixels;
    int n = dst->w * dst->h;
    int magic_count = 0;
    for (int i = 0; i < n; ++i) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(px[i], dst->format, &r, &g, &b, &a);
        if (isMagicPixel(r, g, b)) {
            px[i] = SDL_MapRGBA(dst->format, r, g, b, 0);
            ++magic_count;
        }
    }
    SDL_UnlockSurface(dst);
    log_msg("Magic pixels made transparent: " + std::to_string(magic_count));
    return dst;
}

// ==================================================================
//                ДИАЛОГИ ОТКРЫТИЯ / СОХРАНЕНИЯ
// ==================================================================
#ifdef _WIN32
static std::string openFileDialogW() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Images\0*.png;*.gif;*.bmp;*.jpg;*.jpeg\0All Files\0*.*\0\0";
    ofn.lpstrFile  = filename;
    ofn.nMaxFile   = MAX_PATH;
    ofn.lpstrTitle = L"Open Font Image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) { log_msg("File dialog cancelled"); return ""; }
    int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string r(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, filename, -1, &r[0], len, nullptr, nullptr);
    return r;
}
static std::string saveFileDialogW(const char* defaultExt, const wchar_t* filter) {
    wchar_t filename[MAX_PATH] = L"";
    std::wstring ext;
    if (defaultExt) for (const char* p = defaultExt; *p; ++p) ext.push_back((wchar_t)*p);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Save Font Image";
    ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) { log_msg("Save dialog cancelled"); return ""; }
    int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string r(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, filename, -1, &r[0], len, nullptr, nullptr);
    return r;
}
#else
static std::string openFileDialogW() { return ""; }
static std::string saveFileDialogW(const char*, const wchar_t*) { return ""; }
#endif

// ==================================================================
//                     ЛОКАЛИЗАЦИЯ
// ==================================================================
enum Lang { LANG_EN = 0, LANG_RU = 1 };
static Lang g_lang = LANG_EN;

struct UIStr {
    std::string title, no_image, preview, type_hint;
    std::string text_dialog_title, text_dialog_hint;
    std::string paint_hint;
    std::string tool_pencil, tool_eraser, tool_fill, tool_picker;
    std::string apply, cancel, undo, transparent;
    std::string status_hint, lang_tag;
    std::string help_title, help_hotkeys, help_about, help_close;
    std::string about_text;
};
static const UIStr STR_EN = {
    "OpenBOR Font Editor",
    "No image loaded.  Ctrl+O: open   Drag&Drop: also works",
    "PREVIEW",
    "T: type test text   Enter: apply   Esc: cancel",
    "TEST TEXT",
    "Enter: apply   Esc: cancel   Shift+Enter: new line",
    "LMB: draw   RMB: menu   Alt+LMB: pick   Wheel: zoom   Ctrl+Z: undo",
    "Pencil","Eraser","Fill","Picker","APPLY","CANCEL","UNDO","(transparent)",
    "H: help   L: lang   T: text   E: paint   RMB: menu   Ctrl+O: open   Ctrl+S: save",
    "EN",
    "HELP",
    "HOTKEYS",
    "ABOUT",
    "Close (H / Esc)",
    "OpenBOR Font Editor is a tool for creating and editing bitmap fonts for the OpenBOR engine. Supports indexed PNG / GIF / PCX saving, sprite editor, live preview and Russian / English UI.||Created by MKLIUKANG1."
};
static const UIStr STR_RU = {
    "OpenBOR редактор шрифтов",
    "Изображение не загружено.  Ctrl+O: открыть   Можно перетащить файл",
    "ПРЕДПРОСМОТР",
    "T: печать текста   Enter: применить   Esc: отмена",
    "ТЕСТОВЫЙ ТЕКСТ",
    "Enter: применить   Esc: отмена   Shift+Enter: новая строка",
    "ЛКМ: рисовать   ПКМ: меню   Alt+ЛКМ: пипетка   Колесо: zoom   Ctrl+Z: отмена",
    "Карандаш","Ластик","Заливка","Пипетка","ПРИМЕНИТЬ","ОТМЕНА","ОТМЕНИТЬ","(прозрачный)",
    "H: справка   L: язык   T: текст   E: рисовать   ПКМ: меню   Ctrl+O: открыть   Ctrl+S: сохранить",
    "РУ",
    "СПРАВКА",
    "ГОРЯЧИЕ КЛАВИШИ",
    "О ПРОГРАММЕ",
    "Закрыть (H / Esc)",
    "OpenBOR Font Editor — редактор растровых шрифтов для движка OpenBOR. Сохраняет в индексированные PNG / GIF / PCX, есть спрайт-редактор, живой предпросмотр и русский / английский интерфейс.||Создатель: MKLIUKANG1."
};
static const UIStr& S() { return (g_lang == LANG_RU) ? STR_RU : STR_EN; }

// ==================================================================
//                     СОСТОЯНИЕ
// ==================================================================
struct FontSheet {
    SDL_Surface* surf = nullptr;
    SDL_Texture* tex  = nullptr;
    int charW  = 8;
    int charH  = 8;
    int firstChar   = 0;
    int spacing     = 0;
    int renderScale = 3;
    std::string path;
    int total_chars = 0;
};
struct PaintState {
    bool active  = false;
    int  zoom    = 8, offsetX = 0, offsetY = 0;
    Uint8 color[4] = { 255, 255, 255, 255 };
    int  tool = 0;
    bool drawing = false, dirty = false;
    SDL_Surface* backup = nullptr;
    std::vector<std::vector<Uint32>> undo_stack;
    int lastPaintX = -1, lastPaintY = -1;
};
struct TextDialog { bool active = false; std::string text; };
struct ContextMenu { bool active = false; int x = 0, y = 0, selected = -1; };
struct HelpWindow { bool active = false; };

static FontSheet   g_font;
static PaintState  g_paint;
static TextDialog  g_text_dlg;
static ContextMenu g_ctx;
static HelpWindow  g_help;

static SDL_Window*   g_window   = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static TTF_Font*     g_ui_font  = nullptr;
static TTF_Font*     g_ui_font_bold = nullptr;
static std::string g_test_text = "Hello, World! 0123";

// ==================================================================
//                     ХЕЛПЕРЫ
// ==================================================================
static void unloadFont() {
    if (g_font.tex)  { SDL_DestroyTexture(g_font.tex); g_font.tex = nullptr; }
    if (g_font.surf) { SDL_FreeSurface(g_font.surf);   g_font.surf = nullptr; }
    g_font.total_chars = 0;
}
static void refreshCharCount() {
    if (!g_font.surf) { g_font.total_chars = 0; return; }
    int cols = g_font.surf->w / std::max(1, g_font.charW);
    int rows = g_font.surf->h / std::max(1, g_font.charH);
    g_font.total_chars = cols * rows;
}
static void updateFontTexture() {
    if (!g_font.tex || !g_font.surf) return;
    SDL_UpdateTexture(g_font.tex, nullptr, g_font.surf->pixels, g_font.surf->pitch);
}
static bool loadFont(const std::string& path) {
    if (path.empty()) return false;
    SDL_Surface* raw = IMG_Load(path.c_str());
    if (!raw) { log_msg(std::string("IMG_Load failed: ") + path + " — " + IMG_GetError()); return false; }
    log_msg("Loaded image: " + path + " (" + std::to_string(raw->w) + "x" + std::to_string(raw->h) + ")");

    SDL_Surface* transp = makeTransparentSurface(raw);
    SDL_FreeSurface(raw);
    if (!transp) { log_msg("makeTransparentSurface failed"); return false; }

    unloadFont();
    g_font.surf = transp;
    g_font.tex  = SDL_CreateTextureFromSurface(g_renderer, transp);
    if (!g_font.tex) { log_msg(std::string("CreateTextureFromSurface failed: ") + SDL_GetError()); return false; }
    SDL_SetTextureBlendMode(g_font.tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g_font.tex, SDL_ScaleModeNearest);

    g_font.path = path;
    refreshCharCount();
    SDL_SetWindowTitle(g_window, (S().title + " - " + path).c_str());
    log_msg("Font loaded OK, charW=" + std::to_string(g_font.charW) + " charH=" + std::to_string(g_font.charH));
    return true;
}
static void utf8_pop_back(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size();
    while (i > 0 && ((unsigned char)s[i-1] & 0xC0) == 0x80) --i;
    if (i > 0) --i;
    s.erase(i);
}
static uint32_t utf8_next(const std::string& s, size_t& i) {
    if (i >= s.size()) return 0;
    unsigned char c = (unsigned char)s[i];
    uint32_t cp; int extra;
    if      ((c & 0x80) == 0)    { cp = c;        extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else                         { i++; return '?'; }
    i++;
    for (int j = 0; j < extra && i < s.size(); ++j, ++i)
        cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
    return cp;
}
static void renderText(SDL_Renderer* r, TTF_Font* font,
                       const std::string& text, int x, int y, SDL_Color color) {
    if (!font || text.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst = { x, y, s->w, s->h };
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}
// Многострочный текст с \n
static void renderTextMulti(SDL_Renderer* r, TTF_Font* font,
                            const std::string& text, int x, int y, SDL_Color color,
                            int lineSpacing = 4) {
    if (!font || text.empty()) return;
    int lineH = TTF_FontLineSkip(font) + lineSpacing;
    int cy = y;
    std::string line;
    for (size_t i = 0; i <= text.size(); ) {
        if (i == text.size() || text[i] == '\n') {
            if (!line.empty()) renderText(r, font, line, x, cy, color);
            cy += lineH;
            line.clear();
            if (i < text.size()) ++i;
            else break;
        } else { line.push_back(text[i]); ++i; }
    }
}
static int textWidth(TTF_Font* font, const std::string& text) {
    if (!font || text.empty()) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return w;
}

// Разбивает текст на строки, которые укладываются в maxWidth.
// Понимает разделители абзаца: '\n' и "||".
static std::vector<std::string> wrapText(TTF_Font* font, const std::string& text, int maxWidth) {
    std::vector<std::string> out;
    if (!font || text.empty()) return out;

    // Разделяем на абзацы по "||" и '\n'
    std::vector<std::string> paragraphs;
    std::string cur;
    for (size_t i = 0; i < text.size(); ) {
        if (i + 1 < text.size() && text[i] == '|' && text[i+1] == '|') {
            paragraphs.push_back(cur);
            cur.clear();
            i += 2;
        } else if (text[i] == '\n') {
            paragraphs.push_back(cur);
            cur.clear();
            ++i;
        } else {
            cur.push_back(text[i]);
            ++i;
        }
    }
    if (!cur.empty()) paragraphs.push_back(cur);

    // Переносим каждую строку по словам
    for (auto& p : paragraphs) {
        std::string line;
        std::string word;
        auto flush_word = [&]() {
            if (word.empty()) return;
            std::string test = line.empty() ? word : (line + " " + word);
            if (textWidth(font, test) <= maxWidth) {
                line = test;
            } else {
                if (!line.empty()) out.push_back(line);
                line = word;
            }
            word.clear();
        };
        for (char c : p) {
            if (c == ' ' || c == '\t') {
                flush_word();
            } else {
                word.push_back(c);
            }
        }
        flush_word();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// ==================================================================
//                     СОХРАНЕНИЕ: PNG / GIF / PCX
// ==================================================================
static SDL_Surface* toIndexed8(SDL_Surface* src) {
    if (!src) return nullptr;
    SDL_Palette* pal = src->format->palette;
    if (!pal) {
        SDL_Surface* tmp = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!tmp) return nullptr;
        std::vector<uint32_t> colors;
        SDL_LockSurface(tmp);
        uint32_t* px = (uint32_t*)tmp->pixels;
        int n = tmp->w * tmp->h;
        for (int i = 0; i < n; ++i) {
            uint32_t c = px[i] & 0x00FFFFFF;
            if (std::find(colors.begin(), colors.end(), c) == colors.end()) colors.push_back(c);
            if (colors.size() >= 256) break;
        }
        SDL_UnlockSurface(tmp);
        SDL_Palette* newpal = SDL_AllocPalette((int)std::max<size_t>(1, colors.size()));
        for (size_t i = 0; i < colors.size(); ++i) {
            SDL_Color c;
            c.r = (colors[i] >> 16) & 0xFF;
            c.g = (colors[i] >> 8)  & 0xFF;
            c.b =  colors[i]        & 0xFF;
            c.a = 255;
            newpal->colors[i] = c;
        }
        SDL_Surface* indexed = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 8, SDL_PIXELFORMAT_INDEX8);
        if (!indexed) { SDL_FreeSurface(tmp); SDL_FreePalette(newpal); return nullptr; }
        SDL_SetSurfacePalette(indexed, newpal);
        SDL_LockSurface(tmp);
        SDL_LockSurface(indexed);
        uint32_t* sp = (uint32_t*)tmp->pixels;
        uint8_t*  dp = (uint8_t*)indexed->pixels;
        for (int i = 0; i < n; ++i) {
            uint32_t c = sp[i] & 0x00FFFFFF;
            for (size_t j = 0; j < colors.size(); ++j) if (colors[j] == c) { dp[i] = (uint8_t)j; break; }
        }
        SDL_UnlockSurface(indexed);
        SDL_UnlockSurface(tmp);
        SDL_FreeSurface(tmp);
        SDL_FreePalette(newpal);
        return indexed;
    }
    return SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_INDEX8, 0);
}
static bool savePNG_Indexed(SDL_Surface* src, const char* path) {
    if (!src || !path) return false;
    SDL_Surface* idx = toIndexed8(src);
    if (!idx) return false;
    FILE* fp = fopen(path, "wb");
    if (!fp) { SDL_FreeSurface(idx); return false; }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(fp); SDL_FreeSurface(idx); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); fclose(fp); SDL_FreeSurface(idx); return false; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); fclose(fp); SDL_FreeSurface(idx); return false; }
    png_init_io(png, fp);
    png_set_IHDR(png, info, idx->w, idx->h, 8, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    SDL_Palette* pal = idx->format->palette;
    if (pal && pal->ncolors > 0) {
        int ncol = std::min(pal->ncolors, 256);
        png_color* palette = (png_color*)png_malloc(png, ncol * sizeof(png_color));
        png_byte*  alpha   = (png_byte*) png_malloc(png, ncol * sizeof(png_byte));
        for (int i = 0; i < ncol; ++i) {
            palette[i].red   = pal->colors[i].r;
            palette[i].green = pal->colors[i].g;
            palette[i].blue  = pal->colors[i].b;
            alpha[i]         = pal->colors[i].a;
        }
        png_set_PLTE(png, info, palette, ncol);
        png_set_tRNS(png, info, alpha, ncol, nullptr);
        png_free(png, palette);
        png_free(png, alpha);
    }
    png_write_info(png, info);
    SDL_LockSurface(idx);
    std::vector<png_bytep> rows(idx->h);
    for (int y = 0; y < idx->h; ++y)
        rows[y] = (png_bytep)((uint8_t*)idx->pixels + y * idx->pitch);
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    SDL_UnlockSurface(idx);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    SDL_FreeSurface(idx);
    return true;
}
static bool saveGIF(SDL_Surface* src, const char* path) {
    if (!src || !path) return false;
#ifdef IMG_SaveGIF
    return IMG_SaveGIF(src, path);
#else
    SDL_SetError("IMG_SaveGIF not available");
    return false;
#endif
}
#pragma pack(push, 1)
struct PCXHeader {
    uint8_t  manufacturer, version, encoding, bitsPerPixel;
    uint16_t xmin, ymin, xmax, ymax;
    uint16_t hres, vres;
    uint8_t  palette16[48], reserved, nplanes;
    uint16_t bytesPerLine, paletteInfo;
    uint16_t hscreenSize, vscreenSize;
    uint8_t  filler[54];
};
#pragma pack(pop)
static bool savePCX(SDL_Surface* src, const char* path) {
    if (!src || !path) return false;
    SDL_Surface* idx = toIndexed8(src);
    if (!idx) return false;
    FILE* fp = fopen(path, "wb");
    if (!fp) { SDL_FreeSurface(idx); return false; }
    PCXHeader hdr; memset(&hdr, 0, sizeof(hdr));
    hdr.manufacturer = 10; hdr.version = 5; hdr.encoding = 1; hdr.bitsPerPixel = 8;
    hdr.xmax = (uint16_t)(idx->w - 1); hdr.ymax = (uint16_t)(idx->h - 1);
    hdr.hres = 300; hdr.vres = 300; hdr.nplanes = 1;
    hdr.bytesPerLine = (uint16_t)((idx->w + 1) & ~1);
    hdr.paletteInfo = 1;
    fwrite(&hdr, sizeof(hdr), 1, fp);
    SDL_LockSurface(idx);
    for (int y = 0; y < idx->h; ++y) {
        uint8_t* line = (uint8_t*)idx->pixels + y * idx->pitch;
        int x = 0;
        while (x < hdr.bytesPerLine) {
            uint8_t val = (x < idx->w) ? line[x] : 0;
            int run = 1;
            while (x + run < hdr.bytesPerLine && ((x + run < idx->w) ? line[x + run] : 0) == val && run < 63) ++run;
            if (run > 1 || (val & 0xC0) == 0xC0) { fputc(0xC0 | run, fp); fputc(val, fp); }
            else fputc(val, fp);
            x += run;
        }
    }
    SDL_UnlockSurface(idx);
    fputc(0x0C, fp);
    SDL_Palette* pal = idx->format->palette;
    for (int i = 0; i < 256; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (pal && i < pal->ncolors) { r = pal->colors[i].r; g = pal->colors[i].g; b = pal->colors[i].b; }
        fputc(r, fp); fputc(g, fp); fputc(b, fp);
    }
    fclose(fp);
    SDL_FreeSurface(idx);
    return true;
}

// ==================================================================
//                     РИСОВАНИЕ
// ==================================================================
static void drawSheet(SDL_Renderer* r, int offsetX, int offsetY) {
    if (!g_font.tex || !g_font.surf) {
        renderText(r, g_ui_font, S().no_image, offsetX + 20, offsetY + 20, SDL_Color{120,120,140,255});
        return;
    }
    int sw = g_font.surf->w * g_font.renderScale;
    int sh = g_font.surf->h * g_font.renderScale;
    int tile = 8;
    for (int y = 0; y < sh; y += tile)
        for (int x = 0; x < sw; x += tile) {
            bool dark = ((x / tile) + (y / tile)) % 2 == 0;
            SDL_SetRenderDrawColor(r, dark ? 40 : 55, dark ? 40 : 55, dark ? 50 : 65, 255);
            SDL_Rect rc = { offsetX + x, offsetY + y, std::min(tile, sw - x), std::min(tile, sh - y) };
            SDL_RenderFillRect(r, &rc);
        }
    SDL_Rect dst = { offsetX, offsetY, sw, sh };
    SDL_RenderCopy(r, g_font.tex, nullptr, &dst);

    int cw = g_font.charW * g_font.renderScale;
    int ch = g_font.charH * g_font.renderScale;
    SDL_SetRenderDrawColor(r, 100, 140, 200, 180);
    if (cw > 0) for (int x = 0; x <= sw; x += cw) SDL_RenderDrawLine(r, offsetX + x, offsetY, offsetX + x, offsetY + sh);
    if (ch > 0) for (int y = 0; y <= sh; y += ch) SDL_RenderDrawLine(r, offsetX, offsetY + y, offsetX + sw, offsetY + y);

    SDL_SetRenderDrawColor(r, 160, 200, 255, 255);
    SDL_Rect border = { offsetX - 1, offsetY - 1, sw + 2, sh + 2 };
    SDL_RenderDrawRect(r, &border);
}
static void drawPreviewText(SDL_Renderer* r, int x, int y) {
    if (!g_font.tex || !g_font.surf) return;
    int cw = g_font.charW, ch = g_font.charH;
    if (cw <= 0 || ch <= 0) return;
    int cols = g_font.surf->w / cw;
    int rows = g_font.surf->h / ch;
    if (cols <= 0) return;
    int scale = g_font.renderScale;
    int lineX = x, penY = y;
    for (size_t i = 0; i < g_test_text.size(); ) {
        uint32_t cp = utf8_next(g_test_text, i);
        if (cp == '\n') { lineX = x; penY += ch * scale + 4; continue; }
        int idx = (int)cp - g_font.firstChar;
        if (idx >= 0 && idx < cols * rows) {
            int col = idx % cols;
            int row = idx / cols;
            SDL_Rect src = { col * cw, row * ch, cw, ch };
            SDL_Rect dst = { lineX, penY, cw * scale, ch * scale };
            SDL_RenderCopy(r, g_font.tex, &src, &dst);
        }
        lineX += cw * scale + g_font.spacing * scale;
    }
}
static void drawNormalUI() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    SDL_SetRenderDrawColor(g_renderer, 18, 20, 30, 255);
    SDL_RenderClear(g_renderer);
    drawSheet(g_renderer, 20, 40);

    int previewY = h - 160;
    SDL_SetRenderDrawColor(g_renderer, 24, 28, 40, 255);
    SDL_Rect prev_bg = { 0, previewY, w, h - previewY };
    SDL_RenderFillRect(g_renderer, &prev_bg);
    SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
    SDL_RenderDrawLine(g_renderer, 0, previewY, w, previewY);

    SDL_Color label = { 140, 180, 240, 255 };
    renderText(g_renderer, g_ui_font, S().preview, 20, previewY + 8, label);
    drawPreviewText(g_renderer, 20, previewY + 34);
    renderText(g_renderer, g_ui_font, S().type_hint, 20, previewY + 120, SDL_Color{120,140,180,255});

    // Верхняя статус-панель
    SDL_SetRenderDrawColor(g_renderer, 30, 36, 52, 255);
    SDL_Rect top = { 0, 0, w, 32 };
    SDL_RenderFillRect(g_renderer, &top);

    char status[256];
    snprintf(status, sizeof(status),
        "charW=%d  charH=%d  first=%d  spacing=%d  scale=%d  chars=%d  [%s]",
        g_font.charW, g_font.charH, g_font.firstChar,
        g_font.spacing, g_font.renderScale, g_font.total_chars,
        S().lang_tag.c_str());
    renderText(g_renderer, g_ui_font, status, 12, 8, label);

    // Нижняя строка подсказок — короткая и всегда целиком
    SDL_SetRenderDrawColor(g_renderer, 24, 28, 40, 255);
    SDL_Rect botbg = { 0, h - 22, w, 22 };
    SDL_RenderFillRect(g_renderer, &botbg);
    SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
    SDL_RenderDrawLine(g_renderer, 0, h - 22, w, h - 22);

    renderText(g_renderer, g_ui_font, S().status_hint, 12, h - 18, SDL_Color{170,190,220,255});
}

// ==================================================================
//                     HELP / ABOUT
// ==================================================================
static void drawHelp() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 180);
    SDL_Rect full = { 0, 0, w, h };
    SDL_RenderFillRect(g_renderer, &full);

    int dw = std::min(w - 60, 900);
    int dh = std::min(h - 60, 620);
    int dx = (w - dw) / 2;
    int dy = (h - dh) / 2;

    SDL_SetRenderDrawColor(g_renderer, 22, 26, 40, 250);
    SDL_Rect dlg = { dx, dy, dw, dh };
    SDL_RenderFillRect(g_renderer, &dlg);

    // Заголовок
    SDL_SetRenderDrawColor(g_renderer, 40, 60, 100, 255);
    SDL_Rect hdr = { dx, dy, dw, 40 };
    SDL_RenderFillRect(g_renderer, &hdr);
    TTF_Font* fb = g_ui_font_bold ? g_ui_font_bold : g_ui_font;
    renderText(g_renderer, fb, S().help_title, dx + 16, dy + 10, SDL_Color{200,220,255,255});

    // Рабочая область (между заголовком и кнопкой)
    int contentX = dx + 20;
    int contentY = dy + 54;
    int contentW = dw - 40;
    int contentH = dh - 54 - 60;

    SDL_Rect clipRect = { contentX, contentY, contentW, contentH };
    SDL_RenderSetClipRect(g_renderer, &clipRect);

    int lineH = 20;
    int y = contentY;

    // ---- HOTKEYS ----
    renderText(g_renderer, fb, S().help_hotkeys, contentX, y, SDL_Color{140,180,240,255});
    y += 28;

    const char* keysEN[] = {
        "Ctrl+O", "Ctrl+S", "Ctrl+Shift+S", "Ctrl+Alt+S",
        "F1 / F2", "F3 / F4", "F5 / F6", "F7 / F8", "F9 / F10",
        "L", "T", "E", "H",
        "RMB", "1..4 (in paint)", "Ctrl+Z (in paint)", "Esc"
    };
    const char* descEN[] = {
        "Open image",
        "Save as indexed PNG",
        "Save as GIF",
        "Save as PCX",
        "Character width -/+",
        "Character height -/+",
        "First character code -/+",
        "Display scale -/+",
        "Spacing between characters -/+",
        "Toggle language (EN / RU)",
        "Text input dialog",
        "Paint editor",
        "Help / About (this window)",
        "Context menu (Open / Save / Exit)",
        "Switch paint tool",
        "Undo in paint",
        "Close dialog / exit"
    };
    const char* keysRU[] = {
        "Ctrl+O", "Ctrl+S", "Ctrl+Shift+S", "Ctrl+Alt+S",
        "F1 / F2", "F3 / F4", "F5 / F6", "F7 / F8", "F9 / F10",
        "L", "T", "E", "H",
        "ПКМ", "1..4 (в редакторе)", "Ctrl+Z (в редакторе)", "Esc"
    };
    const char* descRU[] = {
        "Открыть изображение",
        "Сохранить в индексированный PNG",
        "Сохранить в GIF",
        "Сохранить в PCX",
        "Ширина символа -/+",
        "Высота символа -/+",
        "Код первого символа -/+",
        "Масштаб отображения -/+",
        "Интервал между символами -/+",
        "Переключить язык (RU / EN)",
        "Окно ввода текста",
        "Редактор (Paint)",
        "Справка / О программе (это окно)",
        "Контекстное меню (Открыть / Сохранить / Выход)",
        "Сменить инструмент",
        "Отмена в редакторе",
        "Закрыть окно / выход"
    };
    const char** keys = (g_lang == LANG_RU) ? keysRU : keysEN;
    const char** desc = (g_lang == LANG_RU) ? descRU : descEN;

    int keyColW = 180;   // ширина колонки с клавишей
    for (int i = 0; i < 17; ++i) {
        renderText(g_renderer, g_ui_font, keys[i], contentX, y, SDL_Color{240,220,120,255});
        renderText(g_renderer, g_ui_font, desc[i], contentX + keyColW, y, SDL_Color{220,230,250,255});
        y += lineH;
    }

    // ---- ABOUT ----
    y += 16;
    renderText(g_renderer, fb, S().help_about, contentX, y, SDL_Color{140,180,240,255});
    y += 28;

    auto lines = wrapText(g_ui_font, S().about_text, contentW);
    for (auto& ln : lines) {
        renderText(g_renderer, g_ui_font, ln, contentX, y, SDL_Color{210,220,240,255});
        y += lineH;
        if (y > contentY + contentH - lineH) break;
    }

    SDL_RenderSetClipRect(g_renderer, nullptr);

    // ---- Кнопка Закрыть ----
    int btnW = 200;
    int btnH = 34;
    int btnX = dx + (dw - btnW) / 2;
    int btnY = dy + dh - 48;
    SDL_SetRenderDrawColor(g_renderer, 55, 100, 160, 255);
    SDL_Rect btn = { btnX, btnY, btnW, btnH };
    SDL_RenderFillRect(g_renderer, &btn);
    SDL_SetRenderDrawColor(g_renderer, 120, 180, 240, 255);
    SDL_RenderDrawRect(g_renderer, &btn);
    int tw = textWidth(g_ui_font, S().help_close);
    renderText(g_renderer, g_ui_font, S().help_close,
               btnX + (btnW - tw) / 2, btnY + 9, SDL_Color{240,245,255,255});

    // Внешняя рамка
    SDL_SetRenderDrawColor(g_renderer, 120, 170, 240, 255);
    SDL_RenderDrawRect(g_renderer, &dlg);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

// ==================================================================
//                     КОНТЕКСТНОЕ МЕНЮ
// ==================================================================
static const char* CTX_ITEMS_EN[5] = { "Open...", "Save as PNG...", "Save as GIF...", "Save as PCX...", "Exit" };
static const char* CTX_ITEMS_RU[5] = { "Открыть...", "Сохранить PNG...", "Сохранить GIF...", "Сохранить PCX...", "Выход" };

static int ctx_item_height() { return 26; }
static int ctx_menu_width()  { return 210; }
static int ctx_menu_height() { return 5 * ctx_item_height() + 8; }

static void drawContextMenu() {
    if (!g_ctx.active) return;
    const char** items = (g_lang == LANG_RU) ? CTX_ITEMS_RU : CTX_ITEMS_EN;
    int itemH = ctx_item_height();
    int menuW = ctx_menu_width();
    int menuH = ctx_menu_height();

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 20, 24, 34, 250);
    SDL_Rect bg = { g_ctx.x, g_ctx.y, menuW, menuH };
    SDL_RenderFillRect(g_renderer, &bg);
    SDL_SetRenderDrawColor(g_renderer, 80, 120, 180, 255);
    SDL_RenderDrawRect(g_renderer, &bg);

    SDL_Color txt = { 220, 230, 250, 255 };
    for (int i = 0; i < 5; ++i) {
        int iy = g_ctx.y + 4 + i * itemH;
        if (i == g_ctx.selected) {
            SDL_SetRenderDrawColor(g_renderer, 50, 80, 140, 255);
            SDL_Rect sel = { g_ctx.x + 2, iy, menuW - 4, itemH };
            SDL_RenderFillRect(g_renderer, &sel);
        }
        renderText(g_renderer, g_ui_font, items[i], g_ctx.x + 12, iy + 4, txt);
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}
static void ctx_execute_item(int item, bool& running_out) {
    running_out = false;
    switch (item) {
        case 0: {
            std::string path = openFileDialogW();
            if (!path.empty()) loadFont(path);
            break;
        }
        case 1:
            if (g_font.surf) {
                std::string path = saveFileDialogW("png", L"Indexed PNG\0*.png\0All Files\0*.*\0\0");
                if (!path.empty()) {
                    if (savePNG_Indexed(g_font.surf, path.c_str())) log_msg("Saved PNG: " + path);
                }
            }
            break;
        case 2:
            if (g_font.surf) {
                std::string path = saveFileDialogW("gif", L"GIF image\0*.gif\0All Files\0*.*\0\0");
                if (!path.empty()) {
                    if (saveGIF(g_font.surf, path.c_str())) log_msg("Saved GIF: " + path);
                    else log_msg(std::string("GIF save FAILED: ") + SDL_GetError());
                }
            }
            break;
        case 3:
            if (g_font.surf) {
                std::string path = saveFileDialogW("pcx", L"PCX image\0*.pcx\0All Files\0*.*\0\0");
                if (!path.empty()) {
                    if (savePCX(g_font.surf, path.c_str())) log_msg("Saved PCX: " + path);
                }
            }
            break;
        case 4: running_out = true; break;
    }
}

// ==================================================================
//                     ТЕКСТОВЫЙ ДИАЛОГ
// ==================================================================
static void drawTextDialog() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 170);
    SDL_Rect full = { 0, 0, w, h };
    SDL_RenderFillRect(g_renderer, &full);

    int dw = std::min(w - 80, 800);
    int dh = 420;
    int dx = (w - dw) / 2;
    int dy = (h - dh) / 2;

    SDL_SetRenderDrawColor(g_renderer, 22, 26, 40, 250);
    SDL_Rect dlg = { dx, dy, dw, dh };
    SDL_RenderFillRect(g_renderer, &dlg);
    SDL_SetRenderDrawColor(g_renderer, 40, 60, 100, 255);
    SDL_Rect hdr = { dx, dy, dw, 32 };
    SDL_RenderFillRect(g_renderer, &hdr);
    renderText(g_renderer, g_ui_font, S().text_dialog_title, dx + 12, dy + 8, SDL_Color{180,210,255,255});

    SDL_SetRenderDrawColor(g_renderer, 10, 12, 20, 255);
    SDL_Rect box = { dx + 16, dy + 48, dw - 32, 80 };
    SDL_RenderFillRect(g_renderer, &box);
    SDL_SetRenderDrawColor(g_renderer, 80, 120, 190, 255);
    SDL_RenderDrawRect(g_renderer, &box);

    SDL_Color txt = { 240, 245, 255, 255 };
    int tx = box.x + 8;
    int ty = box.y + 6;
    int lineH = g_ui_font ? TTF_FontLineSkip(g_ui_font) : 16;
    std::string line;
    for (size_t i = 0; i <= g_text_dlg.text.size(); ) {
        if (i == g_text_dlg.text.size() || g_text_dlg.text[i] == '\n') {
            renderText(g_renderer, g_ui_font, line, tx, ty, txt);
            ty += lineH;
            line.clear();
            if (i < g_text_dlg.text.size()) ++i;
            else break;
        } else { line.push_back(g_text_dlg.text[i]); ++i; }
    }

    SDL_Rect pvBox = { dx + 16, dy + 144, dw - 32, 220 };
    SDL_SetRenderDrawColor(g_renderer, 8, 10, 16, 255);
    SDL_RenderFillRect(g_renderer, &pvBox);
    SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
    SDL_RenderDrawRect(g_renderer, &pvBox);
    renderText(g_renderer, g_ui_font, "LIVE PREVIEW:", pvBox.x + 6, pvBox.y + 4, SDL_Color{140,180,240,255});

    if (g_font.tex && g_font.surf) {
        int cw = g_font.charW, ch = g_font.charH;
        int cols = g_font.surf->w / cw;
        int rows = g_font.surf->h / ch;
        int scale = 2;
        int lineX = pvBox.x + 8;
        int penY  = pvBox.y + 26;
        for (size_t i = 0; i < g_text_dlg.text.size(); ) {
            uint32_t cp = utf8_next(g_text_dlg.text, i);
            if (cp == '\n') { lineX = pvBox.x + 8; penY += ch * scale + 4; continue; }
            int idx = (int)cp - g_font.firstChar;
            if (idx >= 0 && idx < cols * rows) {
                int col = idx % cols;
                int row = idx / cols;
                SDL_Rect src = { col * cw, row * ch, cw, ch };
                SDL_Rect dst = { lineX, penY, cw * scale, ch * scale };
                SDL_RenderCopy(g_renderer, g_font.tex, &src, &dst);
            }
            lineX += cw * scale + g_font.spacing * scale;
            if (lineX > pvBox.x + pvBox.w - cw * scale) { lineX = pvBox.x + 8; penY += ch * scale + 4; }
        }
    }

    if ((SDL_GetTicks() / 500) % 2 == 0) {
        int curW = textWidth(g_ui_font, line);
        SDL_SetRenderDrawColor(g_renderer, 200, 230, 255, 255);
        SDL_Rect cursor = { tx + curW + 1, ty, 2, lineH - 2 };
        SDL_RenderFillRect(g_renderer, &cursor);
    }

    renderText(g_renderer, g_ui_font, S().text_dialog_hint, dx + 16, dy + dh - 28, SDL_Color{140,170,210,255});
    SDL_SetRenderDrawColor(g_renderer, 120, 170, 240, 255);
    SDL_RenderDrawRect(g_renderer, &dlg);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

// ==================================================================
//                     PAINT
// ==================================================================
struct RGB { Uint8 r, g, b; };
static const RGB PALETTE[24] = {
    {0,0,0}, {64,64,64}, {128,128,128}, {192,192,192}, {255,255,255}, {128,0,0}, {200,0,0}, {255,80,80},
    {255,128,0}, {255,200,0}, {255,255,0}, {128,128,0}, {0,128,0}, {0,200,80}, {0,255,128}, {0,200,200},
    {0,128,255}, {0,0,255}, {64,0,160}, {128,0,255}, {200,0,200}, {255,0,255}, {255,0,128}, {128,64,32}
};
static void paintSnapshot() {
    if (!g_font.surf) return;
    int n = g_font.surf->w * g_font.surf->h;
    std::vector<Uint32> snap(n);
    SDL_LockSurface(g_font.surf);
    Uint32* px = (Uint32*)g_font.surf->pixels;
    int stride = g_font.surf->pitch / 4;
    for (int y = 0; y < g_font.surf->h; ++y)
        for (int x = 0; x < g_font.surf->w; ++x)
            snap[y * g_font.surf->w + x] = px[y * stride + x];
    SDL_UnlockSurface(g_font.surf);
    g_paint.undo_stack.push_back(std::move(snap));
    if (g_paint.undo_stack.size() > 15) g_paint.undo_stack.erase(g_paint.undo_stack.begin());
}
static void paintUndo() {
    if (!g_font.surf || g_paint.undo_stack.empty()) return;
    auto& snap = g_paint.undo_stack.back();
    SDL_LockSurface(g_font.surf);
    Uint32* px = (Uint32*)g_font.surf->pixels;
    int stride = g_font.surf->pitch / 4;
    for (int y = 0; y < g_font.surf->h; ++y)
        for (int x = 0; x < g_font.surf->w; ++x)
            px[y * stride + x] = snap[y * g_font.surf->w + x];
    SDL_UnlockSurface(g_font.surf);
    g_paint.undo_stack.pop_back();
    updateFontTexture();
    g_paint.dirty = true;
}
static void paintPutPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (!g_font.surf) return;
    if (x < 0 || x >= g_font.surf->w || y < 0 || y >= g_font.surf->h) return;
    SDL_LockSurface(g_font.surf);
    Uint32* px = (Uint32*)g_font.surf->pixels;
    int stride = g_font.surf->pitch / 4;
    px[y * stride + x] = SDL_MapRGBA(g_font.surf->format, r, g, b, a);
    SDL_UnlockSurface(g_font.surf);
}
static void paintGetPixel(int x, int y, Uint8& r, Uint8& g, Uint8& b, Uint8& a) {
    r = g = b = 0; a = 255;
    if (!g_font.surf) return;
    if (x < 0 || x >= g_font.surf->w || y < 0 || y >= g_font.surf->h) return;
    SDL_LockSurface(g_font.surf);
    Uint32* px = (Uint32*)g_font.surf->pixels;
    int stride = g_font.surf->pitch / 4;
    SDL_GetRGBA(px[y * stride + x], g_font.surf->format, &r, &g, &b, &a);
    SDL_UnlockSurface(g_font.surf);
}
static void paintFloodFill(int x0, int y0, Uint8 nr, Uint8 ng, Uint8 nb, Uint8 na) {
    if (!g_font.surf) return;
    int W = g_font.surf->w, H = g_font.surf->h;
    if (x0 < 0 || x0 >= W || y0 < 0 || y0 >= H) return;
    SDL_LockSurface(g_font.surf);
    Uint32* px = (Uint32*)g_font.surf->pixels;
    int stride = g_font.surf->pitch / 4;
    Uint32 target = px[y0 * stride + x0];
    Uint32 newC = SDL_MapRGBA(g_font.surf->format, nr, ng, nb, na);
    if (target == newC) { SDL_UnlockSurface(g_font.surf); return; }
    std::vector<std::pair<int,int>> stack;
    stack.reserve(256);
    stack.push_back({ x0, y0 });
    while (!stack.empty()) {
        auto p = stack.back(); stack.pop_back();
        int x = p.first, y = p.second;
        if (x < 0 || x >= W || y < 0 || y >= H) continue;
        if (px[y * stride + x] != target) continue;
        px[y * stride + x] = newC;
        stack.push_back({ x+1, y }); stack.push_back({ x-1, y });
        stack.push_back({ x, y+1 }); stack.push_back({ x, y-1 });
    }
    SDL_UnlockSurface(g_font.surf);
}
static bool paintScreenToPixel(int sx, int sy, int cx, int cy, int cw, int ch, int& px, int& py) {
    int z = g_paint.zoom;
    int fx = (sx - cx - g_paint.offsetX) / z;
    int fy = (sy - cy - g_paint.offsetY) / z;
    if (fx < 0 || fx >= g_font.surf->w) return false;
    if (fy < 0 || fy >= g_font.surf->h) return false;
    px = fx; py = fy;
    return true;
}
static void drawPaintEditor() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    SDL_SetRenderDrawColor(g_renderer, 30, 34, 46, 255);
    SDL_RenderClear(g_renderer);

    int topH = 40, palW = 160;
    int canvasX = 0, canvasY = topH, canvasW = w - palW, canvasH = h - topH - 24;
    SDL_Rect clip = { canvasX, canvasY, canvasW, canvasH };
    SDL_RenderSetClipRect(g_renderer, &clip);
    SDL_SetRenderDrawColor(g_renderer, 22, 24, 32, 255);
    SDL_Rect cvBg = { canvasX, canvasY, canvasW, canvasH };
    SDL_RenderFillRect(g_renderer, &cvBg);

    if (g_font.surf && g_font.tex) {
        int tile = 16;
        for (int y = 0; y < canvasH; y += tile)
            for (int x = 0; x < canvasW; x += tile) {
                bool dark = ((x/tile)+(y/tile))%2==0;
                SDL_SetRenderDrawColor(g_renderer, dark?35:45, dark?35:45, dark?42:55, 255);
                SDL_Rect rc = { canvasX+x, canvasY+y, std::min(tile, canvasW-x), std::min(tile, canvasH-y) };
                SDL_RenderFillRect(g_renderer, &rc);
            }
        int z = g_paint.zoom;
        int sw = g_font.surf->w * z, sh = g_font.surf->h * z;
        SDL_Rect dst = { canvasX + g_paint.offsetX, canvasY + g_paint.offsetY, sw, sh };
        SDL_RenderCopy(g_renderer, g_font.tex, nullptr, &dst);
        if (z >= 4) {
            SDL_SetRenderDrawColor(g_renderer, 80, 100, 140, 120);
            int gx0 = canvasX + g_paint.offsetX, gy0 = canvasY + g_paint.offsetY;
            for (int x = 0; x <= g_font.surf->w; ++x) {
                int sx = gx0 + x * z;
                if (sx < canvasX || sx > canvasX + canvasW) continue;
                SDL_RenderDrawLine(g_renderer, sx, gy0, sx, gy0 + sh);
            }
            for (int y = 0; y <= g_font.surf->h; ++y) {
                int sy = gy0 + y * z;
                if (sy < canvasY || sy > canvasY + canvasH) continue;
                SDL_RenderDrawLine(g_renderer, gx0, sy, gx0 + sw, sy);
            }
        }
        int mx, my; SDL_GetMouseState(&mx, &my);
        if (mx >= canvasX && mx < canvasX + canvasW && my >= canvasY && my < canvasY + canvasH) {
            int fx, fy;
            if (paintScreenToPixel(mx, my, canvasX, canvasY, canvasW, canvasH, fx, fy)) {
                int hx = canvasX + g_paint.offsetX + fx * z;
                int hy = canvasY + g_paint.offsetY + fy * z;
                SDL_SetRenderDrawColor(g_renderer, 255, 80, 80, 255);
                SDL_Rect hi = { hx, hy, z, z };
                SDL_RenderDrawRect(g_renderer, &hi);
            }
        }
        SDL_SetRenderDrawColor(g_renderer, 160, 200, 255, 255);
        SDL_RenderDrawRect(g_renderer, &dst);
    }
    SDL_RenderSetClipRect(g_renderer, nullptr);

    SDL_SetRenderDrawColor(g_renderer, 40, 46, 64, 255);
    SDL_Rect top = { 0, 0, w, topH };
    SDL_RenderFillRect(g_renderer, &top);
    SDL_Color txt = { 220, 230, 250, 255 };
    SDL_Color dim = { 140, 160, 200, 255 };

    const char* toolNames[4] = { S().tool_pencil.c_str(), S().tool_eraser.c_str(),
                                 S().tool_fill.c_str(), S().tool_picker.c_str() };
    int bx = 8;
    for (int i = 0; i < 4; ++i) {
        int bw = textWidth(g_ui_font, toolNames[i]) + 20;
        SDL_Rect btn = { bx, 6, bw, 28 };
        if (i == g_paint.tool) SDL_SetRenderDrawColor(g_renderer, 60, 100, 180, 255);
        else                   SDL_SetRenderDrawColor(g_renderer, 55, 62, 82, 255);
        SDL_RenderFillRect(g_renderer, &btn);
        SDL_SetRenderDrawColor(g_renderer, 90, 120, 180, 255);
        SDL_RenderDrawRect(g_renderer, &btn);
        renderText(g_renderer, g_ui_font, toolNames[i], bx + 10, 12, txt);
        bx += bw + 6;
    }
    bx += 16;
    renderText(g_renderer, g_ui_font, "Zoom:", bx, 12, dim);
    bx += textWidth(g_ui_font, "Zoom:") + 8;
    renderText(g_renderer, g_ui_font, "x" + std::to_string(g_paint.zoom), bx, 12, txt);

    int cancelW = textWidth(g_ui_font, S().cancel) + 24;
    int applyW  = textWidth(g_ui_font, S().apply)  + 24;
    int cancelX = w - cancelW - 8;
    int applyX  = cancelX - applyW - 6;
    SDL_Rect btnCancel = { cancelX, 6, cancelW, 28 };
    SDL_Rect btnApply  = { applyX,  6, applyW,  28 };
    SDL_SetRenderDrawColor(g_renderer, 140, 60, 60, 255);
    SDL_RenderFillRect(g_renderer, &btnCancel);
    SDL_SetRenderDrawColor(g_renderer, 200, 100, 100, 255);
    SDL_RenderDrawRect(g_renderer, &btnCancel);
    renderText(g_renderer, g_ui_font, S().cancel, cancelX + 12, 12, txt);
    SDL_SetRenderDrawColor(g_renderer, 60, 130, 70, 255);
    SDL_RenderFillRect(g_renderer, &btnApply);
    SDL_SetRenderDrawColor(g_renderer, 110, 210, 120, 255);
    SDL_RenderDrawRect(g_renderer, &btnApply);
    renderText(g_renderer, g_ui_font, S().apply, applyX + 12, 12, txt);

    int palX = w - palW;
    SDL_SetRenderDrawColor(g_renderer, 26, 30, 42, 255);
    SDL_Rect palBg = { palX, topH, palW, h - topH };
    SDL_RenderFillRect(g_renderer, &palBg);
    SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
    SDL_RenderDrawLine(g_renderer, palX, topH, palX, h);

    int swatch = 28, gap = 6;
    int px0 = palX + (palW - (8 * swatch + 7 * gap)) / 2;
    int py0 = topH + 16;
    for (int i = 0; i < 24; ++i) {
        int col = i % 8, row = i / 8;
        SDL_Rect rc = { px0 + col * (swatch + gap), py0 + row * (swatch + gap), swatch, swatch };
        SDL_SetRenderDrawColor(g_renderer, PALETTE[i].r, PALETTE[i].g, PALETTE[i].b, 255);
        SDL_RenderFillRect(g_renderer, &rc);
        bool is_cur = g_paint.color[0] == PALETTE[i].r && g_paint.color[1] == PALETTE[i].g &&
                      g_paint.color[2] == PALETTE[i].b && g_paint.color[3] == 255;
        SDL_SetRenderDrawColor(g_renderer, is_cur?255:80, is_cur?220:80, is_cur?100:80, 255);
        SDL_Rect border = { rc.x - 1, rc.y - 1, rc.w + 2, rc.h + 2 };
        SDL_RenderDrawRect(g_renderer, &border);
    }
    int tyP = py0 + 3 * (swatch + gap) + 6;
    SDL_Rect transp = { palX + 12, tyP, palW - 24, 32 };
    bool is_tr = g_paint.color[3] == 0;
    SDL_SetRenderDrawColor(g_renderer, is_tr?70:40, is_tr?110:50, is_tr?190:70, 255);
    SDL_RenderFillRect(g_renderer, &transp);
    SDL_SetRenderDrawColor(g_renderer, 120, 160, 220, 255);
    SDL_RenderDrawRect(g_renderer, &transp);
    renderText(g_renderer, g_ui_font, S().transparent, palX + 20, tyP + 8, txt);
    SDL_Rect undoBtn = { palX + 12, tyP + 40, palW - 24, 28 };
    SDL_SetRenderDrawColor(g_renderer, 55, 62, 82, 255);
    SDL_RenderFillRect(g_renderer, &undoBtn);
    SDL_SetRenderDrawColor(g_renderer, 120, 160, 220, 255);
    SDL_RenderDrawRect(g_renderer, &undoBtn);
    renderText(g_renderer, g_ui_font, S().undo, palX + 20, tyP + 44, txt);

    SDL_SetRenderDrawColor(g_renderer, 26, 30, 42, 255);
    SDL_Rect bot = { 0, h - 24, w, 24 };
    SDL_RenderFillRect(g_renderer, &bot);
    renderText(g_renderer, g_ui_font, S().paint_hint, 8, h - 20, dim);
}
static void drawUI() {
    if (g_paint.active)         drawPaintEditor();
    else if (g_text_dlg.active) { drawNormalUI(); drawTextDialog(); }
    else                        drawNormalUI();
    if (g_ctx.active)           drawContextMenu();
    if (g_help.active)          drawHelp();
    SDL_RenderPresent(g_renderer);
}

// ==================================================================
//                     UI-ШРИФТ
// ==================================================================
static TTF_Font* loadUIFont(int size) {
    const char* candidates[] = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (const char* p : candidates) {
        TTF_Font* f = TTF_OpenFont(p, size);
        if (f) { log_msg(std::string("UI font loaded: ") + p); return f; }
    }
    log_msg("WARNING: no UI font found");
    return nullptr;
}

// ==================================================================
//                     PAINT ENTER / APPLY / CANCEL
// ==================================================================
static void enterPaint() {
    if (!g_font.surf) { log_msg("Paint: no font"); return; }
    if (g_paint.backup) SDL_FreeSurface(g_paint.backup);
    g_paint.backup = SDL_ConvertSurfaceFormat(g_font.surf, g_font.surf->format->format, 0);
    g_paint.active = true;
    g_paint.dirty = false;
    g_paint.offsetX = 20;
    g_paint.offsetY = 20;
    g_paint.undo_stack.clear();
    int w, h;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    int availW = w - 160 - 40;
    int availH = h - 40 - 24 - 40;
    int zx = availW / std::max(1, g_font.surf->w);
    int zy = availH / std::max(1, g_font.surf->h);
    g_paint.zoom = std::max(1, std::min(zx, zy));
    log_msg("Paint mode entered, zoom=" + std::to_string(g_paint.zoom));
}
static void applyPaint() {
    g_paint.active = false;
    g_paint.dirty  = false;
    if (g_paint.backup) { SDL_FreeSurface(g_paint.backup); g_paint.backup = nullptr; }
    g_paint.undo_stack.clear();
    updateFontTexture();
    log_msg("Paint: changes applied");
}
static void cancelPaint() {
    if (g_paint.backup && g_font.surf) {
        SDL_ConvertPixels(g_paint.backup->w, g_paint.backup->h,
                          g_paint.backup->format->format,
                          g_paint.backup->pixels, g_paint.backup->pitch,
                          g_font.surf->format->format,
                          g_font.surf->pixels, g_font.surf->pitch);
        updateFontTexture();
    }
    g_paint.active = false;
    g_paint.dirty  = false;
    if (g_paint.backup) { SDL_FreeSurface(g_paint.backup); g_paint.backup = nullptr; }
    g_paint.undo_stack.clear();
    log_msg("Paint: cancelled");
}

// ==================================================================
//                     MAIN
// ==================================================================
int main(int argc, char** argv) {
    log_init();
    log_msg("OpenBOR Font Editor starting...");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { log_msg(std::string("SDL_Init: ") + SDL_GetError()); return 1; }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();

    g_window = SDL_CreateWindow(S().title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 700,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_window) { log_msg("CreateWindow failed"); return 1; }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) { log_msg("CreateRenderer failed"); return 1; }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    g_ui_font = loadUIFont(14);
    g_ui_font_bold = loadUIFont(16);
    // Иконка окна из icon.png
    {
        SDL_Surface* icon = IMG_Load("icon.png");
        if (icon) {
            SDL_SetWindowIcon(g_window, icon);
            SDL_FreeSurface(icon);
            log_msg("Window icon set from icon.png");
        } else {
            log_msg(std::string("icon.png not loaded: ") + IMG_GetError());
        }
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);

    if (argc > 1) loadFont(argv[1]);

    bool running = true;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; continue; }

            // ---- HELP ----
            if (g_help.active) {
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_ESCAPE || k == SDLK_h) g_help.active = false;
                }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int w, h; SDL_GetRendererOutputSize(g_renderer, &w, &h);
                    int dw = std::min(w - 80, 820);
                    int dh = std::min(h - 80, 560);
                    int dx = (w - dw) / 2;
                    int dy = (h - dh) / 2;
                    int btnW = 180, btnH = 32;
                    int btnX = dx + (dw - btnW) / 2;
                    int btnY = dy + dh - 50;
                    if (e.button.x >= btnX && e.button.x < btnX + btnW &&
                        e.button.y >= btnY && e.button.y < btnY + btnH) {
                        g_help.active = false;
                    } else if (e.button.x < dx || e.button.x > dx + dw ||
                               e.button.y < dy || e.button.y > dy + dh) {
                        g_help.active = false;
                    }
                }
                continue;
            }

            // ---- CONTEXT MENU ----
            if (g_ctx.active) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int my = e.button.y - g_ctx.y;
                    int item = (my - 4) / ctx_item_height();
                    if (item >= 0 && item < 5) {
                        g_ctx.active = false;
                        bool quit = false;
                        ctx_execute_item(item, quit);
                        if (quit) running = false;
                    } else g_ctx.active = false;
                }
                else if (e.type == SDL_MOUSEMOTION) {
                    int my = e.motion.y - g_ctx.y;
                    int item = (my - 4) / ctx_item_height();
                    g_ctx.selected = (item >= 0 && item < 5) ? item : -1;
                }
                else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                    g_ctx.active = false;
                }
                continue;
            }

            // ---- TEXT DIALOG ----
            if (g_text_dlg.active) {
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    SDL_Keymod mod = (SDL_Keymod)e.key.keysym.mod;
                    if (k == SDLK_ESCAPE) { g_text_dlg.active = false; SDL_StopTextInput(); }
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        if (mod & KMOD_SHIFT) g_text_dlg.text += '\n';
                        else {
                            g_test_text = g_text_dlg.text;
                            g_text_dlg.active = false;
                            SDL_StopTextInput();
                            log_msg("Test text: " + g_test_text);
                        }
                    }
                    else if (k == SDLK_BACKSPACE) utf8_pop_back(g_text_dlg.text);
                }
                else if (e.type == SDL_TEXTINPUT) g_text_dlg.text += e.text.text;
                continue;
            }

            // ---- PAINT ----
            if (g_paint.active) {
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    SDL_Keymod mod = (SDL_Keymod)e.key.keysym.mod;
                    if (k == SDLK_ESCAPE) cancelPaint();
                    else if (k == SDLK_RETURN) applyPaint();
                    else if (k == SDLK_z && (mod & KMOD_CTRL)) paintUndo();
                    else if (k == SDLK_1) g_paint.tool = 0;
                    else if (k == SDLK_2) g_paint.tool = 1;
                    else if (k == SDLK_3) g_paint.tool = 2;
                    else if (k == SDLK_4) g_paint.tool = 3;
                    else if (k == SDLK_PLUS || k == SDLK_EQUALS) g_paint.zoom = std::min(64, g_paint.zoom + 1);
                    else if (k == SDLK_MINUS) g_paint.zoom = std::max(1, g_paint.zoom - 1);
                    else if (k == SDLK_h) g_help.active = true;
                }
                else if (e.type == SDL_MOUSEWHEEL) {
                    if (e.wheel.y > 0) g_paint.zoom = std::min(64, g_paint.zoom + 1);
                    else if (e.wheel.y < 0) g_paint.zoom = std::max(1, g_paint.zoom - 1);
                }
                else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION) {
                    int w, h; SDL_GetRendererOutputSize(g_renderer, &w, &h);
                    int palW = 160;
                    int canvasX = 0, canvasY = 40, canvasW = w - palW, canvasH = h - 40 - 24;
                    int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
                    int my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;

                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                        g_ctx.active = true; g_ctx.x = mx; g_ctx.y = my; g_ctx.selected = -1;
                        continue;
                    }

                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && my < 40) {
                        const char* toolNames[4] = { S().tool_pencil.c_str(), S().tool_eraser.c_str(),
                                                     S().tool_fill.c_str(), S().tool_picker.c_str() };
                        int bx = 8; bool handled = false;
                        for (int i = 0; i < 4; ++i) {
                            int bw = textWidth(g_ui_font, toolNames[i]) + 20;
                            if (mx >= bx && mx < bx + bw) { g_paint.tool = i; handled = true; break; }
                            bx += bw + 6;
                        }
                        if (!handled) {
                            int cancelW = textWidth(g_ui_font, S().cancel) + 24;
                            int applyW  = textWidth(g_ui_font, S().apply)  + 24;
                            int cancelX = w - cancelW - 8;
                            int applyX  = cancelX - applyW - 6;
                            if (mx >= cancelX && mx < cancelX + cancelW) cancelPaint();
                            else if (mx >= applyX && mx < applyX + applyW) applyPaint();
                        }
                    }
                    else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                             mx >= w - palW && my >= 40) {
                        int swatch = 28, gap = 6;
                        int px0 = (w - palW) + (palW - (8 * swatch + 7 * gap)) / 2;
                        int py0 = 40 + 16;
                        bool handled = false;
                        for (int i = 0; i < 24; ++i) {
                            int col = i % 8, row = i / 8;
                            int sx = px0 + col * (swatch + gap);
                            int sy = py0 + row * (swatch + gap);
                            if (mx >= sx && mx < sx + swatch && my >= sy && my < sy + swatch) {
                                g_paint.color[0] = PALETTE[i].r;
                                g_paint.color[1] = PALETTE[i].g;
                                g_paint.color[2] = PALETTE[i].b;
                                g_paint.color[3] = 255;
                                handled = true; break;
                            }
                        }
                        if (!handled) {
                            int tyP = py0 + 3 * (swatch + gap) + 6;
                            SDL_Rect transp = { (w - palW) + 12, tyP, palW - 24, 32 };
                            if (mx >= transp.x && mx < transp.x + transp.w &&
                                my >= transp.y && my < transp.y + transp.h) g_paint.color[3] = 0;
                            SDL_Rect undoBtn = { (w - palW) + 12, tyP + 40, palW - 24, 28 };
                            if (mx >= undoBtn.x && mx < undoBtn.x + undoBtn.w &&
                                my >= undoBtn.y && my < undoBtn.y + undoBtn.h) paintUndo();
                        }
                    }
                    else {
                        int fx, fy;
                        bool inCanvas = (mx >= canvasX && mx < canvasX + canvasW &&
                                         my >= canvasY && my < canvasY + canvasH);
                        if (inCanvas && paintScreenToPixel(mx, my, canvasX, canvasY, canvasW, canvasH, fx, fy)) {
                            if (e.type == SDL_MOUSEBUTTONDOWN) {
                                bool lmb = (e.button.button == SDL_BUTTON_LEFT);
                                bool alt = (SDL_GetModState() & KMOD_ALT) != 0;
                                if (alt && lmb) {
                                    Uint8 r, g, b, a;
                                    paintGetPixel(fx, fy, r, g, b, a);
                                    g_paint.color[0] = r; g_paint.color[1] = g;
                                    g_paint.color[2] = b; g_paint.color[3] = a;
                                } else if (g_paint.tool == 1) {
                                    paintSnapshot();
                                    paintPutPixel(fx, fy, 0, 0, 0, 0);
                                    g_paint.drawing = true; g_paint.dirty = true;
                                    updateFontTexture();
                                } else if (g_paint.tool == 2) {
                                    paintSnapshot();
                                    paintFloodFill(fx, fy, g_paint.color[0], g_paint.color[1], g_paint.color[2], g_paint.color[3]);
                                    g_paint.dirty = true; updateFontTexture();
                                } else if (g_paint.tool == 3) {
                                    Uint8 r, g, b, a;
                                    paintGetPixel(fx, fy, r, g, b, a);
                                    g_paint.color[0] = r; g_paint.color[1] = g;
                                    g_paint.color[2] = b; g_paint.color[3] = a;
                                } else {
                                    paintSnapshot();
                                    paintPutPixel(fx, fy, g_paint.color[0], g_paint.color[1], g_paint.color[2], g_paint.color[3]);
                                    g_paint.drawing = true; g_paint.dirty = true;
                                    updateFontTexture();
                                }
                                g_paint.lastPaintX = fx; g_paint.lastPaintY = fy;
                            }
                            else if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK) &&
                                     g_paint.tool == 0 && !(SDL_GetModState() & KMOD_ALT)) {
                                if (fx != g_paint.lastPaintX || fy != g_paint.lastPaintY) {
                                    paintPutPixel(fx, fy, g_paint.color[0], g_paint.color[1], g_paint.color[2], g_paint.color[3]);
                                    g_paint.lastPaintX = fx; g_paint.lastPaintY = fy;
                                    g_paint.dirty = true; updateFontTexture();
                                }
                            }
                        }
                        if (e.type == SDL_MOUSEBUTTONUP) {
                            g_paint.drawing = false;
                            g_paint.lastPaintX = g_paint.lastPaintY = -1;
                        }
                    }
                }
                continue;
            }

            // ---- NORMAL MODE ----
            if (e.type == SDL_DROPFILE) {
                std::string path = e.drop.file ? e.drop.file : "";
                if (e.drop.file) SDL_free(e.drop.file);
                log_msg("DnD: " + path);
                loadFont(path);
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                g_ctx.active = true;
                g_ctx.x = e.button.x;
                g_ctx.y = e.button.y;
                g_ctx.selected = -1;
            }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                SDL_Keymod mod = (SDL_Keymod)e.key.keysym.mod;

                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_h) { g_help.active = true; }
                else if (k == SDLK_o && (mod & KMOD_CTRL)) {
                    std::string path = openFileDialogW();
                    if (!path.empty()) loadFont(path);
                }
                else if (k == SDLK_s && (mod & KMOD_CTRL)) {
                    if (!g_font.surf) { log_msg("Save: no font"); }
                    else {
                        std::string path;
                        if (mod & KMOD_SHIFT) {
                            path = saveFileDialogW("gif", L"GIF image\0*.gif\0All Files\0*.*\0\0");
                            if (!path.empty()) {
                                if (saveGIF(g_font.surf, path.c_str())) log_msg("Saved GIF: " + path);
                            }
                        } else if (mod & KMOD_ALT) {
                            path = saveFileDialogW("pcx", L"PCX image\0*.pcx\0All Files\0*.*\0\0");
                            if (!path.empty()) {
                                if (savePCX(g_font.surf, path.c_str())) log_msg("Saved PCX: " + path);
                            }
                        } else {
                            path = saveFileDialogW("png", L"Indexed PNG\0*.png\0All Files\0*.*\0\0");
                            if (!path.empty()) {
                                if (savePNG_Indexed(g_font.surf, path.c_str())) log_msg("Saved PNG: " + path);
                            }
                        }
                    }
                }
                else if (k == SDLK_l) {
                    g_lang = (g_lang == LANG_EN) ? LANG_RU : LANG_EN;
                    if (g_window) SDL_SetWindowTitle(g_window, S().title.c_str());
                }
                else if (k == SDLK_t) {
                    g_text_dlg.active = true;
                    g_text_dlg.text = g_test_text;
                    SDL_StartTextInput();
                }
                else if (k == SDLK_e) enterPaint();
                else if (k == SDLK_F1)  { g_font.charW = std::max(1, g_font.charW - 1); refreshCharCount(); }
                else if (k == SDLK_F2)  { g_font.charW = std::min(128, g_font.charW + 1); refreshCharCount(); }
                else if (k == SDLK_F3)  { g_font.charH = std::max(1, g_font.charH - 1); refreshCharCount(); }
                else if (k == SDLK_F4)  { g_font.charH = std::min(128, g_font.charH + 1); refreshCharCount(); }
                else if (k == SDLK_F5)  g_font.firstChar = std::max(0, g_font.firstChar - 1);
                else if (k == SDLK_F6)  g_font.firstChar = std::min(0x10FFFF, g_font.firstChar + 1);
                else if (k == SDLK_F7)  g_font.renderScale = std::max(1, g_font.renderScale - 1);
                else if (k == SDLK_F8)  g_font.renderScale = std::min(16, g_font.renderScale + 1);
                else if (k == SDLK_F9)  g_font.spacing = std::max(-10, g_font.spacing - 1);
                else if (k == SDLK_F10) g_font.spacing = std::min(64, g_font.spacing + 1);
            }
        }
        drawUI();
    }

    if (g_paint.backup) SDL_FreeSurface(g_paint.backup);
    unloadFont();
    if (g_ui_font) TTF_CloseFont(g_ui_font);
    if (g_ui_font_bold) TTF_CloseFont(g_ui_font_bold);
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    if (g_log) g_log.close();
    return 0;
}
