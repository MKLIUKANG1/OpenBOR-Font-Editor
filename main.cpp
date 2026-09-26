// main.cpp — OpenBOR Font Editor
// Redesign: тулбар, боковая панель настроек, живой ввод текста,
// hover-подсветка символа, прокрутка листа, аккуратный статусбар
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
//                     МАГИЧЕСКИЙ ЦВЕТ
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
//                     ДИАЛОГИ
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
    std::string title, no_image;
    std::string btn_open, btn_save_png, btn_save_gif, btn_save_pcx;
    std::string btn_paint, btn_help, btn_lang;
    std::string settings_title;
    std::string set_charw, set_charh, set_first, set_spacing, set_scale;
    std::string preview_title, input_hint;
    std::string help_title, help_hotkeys, help_about, help_close;
    std::string about_text;
    std::string paint_hint;
    std::string tool_pencil, tool_eraser, tool_fill, tool_picker;
    std::string apply, cancel, undo, transparent;
    std::string status_no_file, status_file;
    std::string hover_none;
    std::string reset_defaults;
};

static const UIStr STR_EN = {
    "OpenBOR Font Editor",
    "No image loaded. Drag&Drop a font image or click [ Open ]",
    "Open","Save PNG","Save GIF","Save PCX",
    "Paint","Help","EN",
    "SETTINGS",
    "Char width","Char height","First code","Spacing","Scale",
    "LIVE PREVIEW","Type here...",
    "HELP",
    "HOTKEYS",
    "ABOUT",
    "Close (Esc)",
    "OpenBOR Font Editor v1.1 — a bitmap font editor for the OpenBOR engine.||Features: toolbar, side settings panel, live text preview, sprite editor, indexed PNG/GIF/PCX save, English/Russian UI.||Created by MKLIUKANG1.",
    "LMB: draw   RMB: menu   Alt+LMB: pick   Wheel: zoom   Ctrl+Z: undo",
    "Pencil","Eraser","Fill","Picker",
    "APPLY","CANCEL","UNDO","(transparent)",
    "no file loaded","file",
    "hover a cell to see its code",
    "Reset defaults"
};
static const UIStr STR_RU = {
    "OpenBOR редактор шрифтов",
    "Изображение не загружено. Перетащите файл или нажмите [ Открыть ]",
    "Открыть","PNG","GIF","PCX",
    "Рисовать","Справка","РУ",
    "НАСТРОЙКИ",
    "Ширина","Высота","Первый код","Интервал","Масштаб",
    "ЖИВОЙ ПРЕДПРОСМОТР","Введите текст...",
    "СПРАВКА",
    "ГОРЯЧИЕ КЛАВИШИ",
    "О ПРОГРАММЕ",
    "Закрыть (Esc)",
    "OpenBOR Font Editor v1.1 — редактор растровых шрифтов для движка OpenBOR.||Тулбар, боковая панель настроек, живой предпросмотр, спрайт-редактор, сохранение в PNG/GIF/PCX, русский/английский интерфейс.||Создатель: MKLIUKANG1.",
    "ЛКМ: рисовать   ПКМ: меню   Alt+ЛКМ: пипетка   Колесо: zoom   Ctrl+Z: отмена",
    "Карандаш","Ластик","Заливка","Пипетка",
    "ПРИМЕНИТЬ","ОТМЕНА","ОТМЕНИТЬ","(прозрачный)",
    "файл не загружен","файл",
    "наведите на ячейку, чтобы увидеть код",
    "Сбросить настройки"
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
    int cols = 0;
    int rows = 0;
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
struct HelpWindow { bool active = false; };
struct ContextMenu { bool active = false; int x = 0, y = 0, selected = -1; };

static FontSheet   g_font;
static PaintState  g_paint;
static HelpWindow  g_help;
static ContextMenu g_ctx;

static SDL_Window*   g_window   = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static TTF_Font*     g_ui_font  = nullptr;
static TTF_Font*     g_ui_font_bold = nullptr;

// Прокрутка листа
static int g_scroll_x = 0;
static int g_scroll_y = 0;

// Ввод текста
static bool        g_typing = false;
static std::string g_test_text = "Hello, World! 0123";

// Hover-ячейка под курсором
static int  g_hover_col = -1;
static int  g_hover_row = -1;
static bool g_hover_valid = false;

// ==================================================================
//                     ХЕЛПЕРЫ
// ==================================================================
static void unloadFont() {
    if (g_font.tex)  { SDL_DestroyTexture(g_font.tex); g_font.tex = nullptr; }
    if (g_font.surf) { SDL_FreeSurface(g_font.surf);   g_font.surf = nullptr; }
    g_font.total_chars = 0;
    g_font.cols = 0;
    g_font.rows = 0;
    g_scroll_x = 0;
    g_scroll_y = 0;
}
static void refreshCharCount() {
    if (!g_font.surf) { g_font.total_chars = 0; g_font.cols = 0; g_font.rows = 0; return; }
    g_font.cols = g_font.surf->w / std::max(1, g_font.charW);
    g_font.rows = g_font.surf->h / std::max(1, g_font.charH);
    g_font.total_chars = g_font.cols * g_font.rows;
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
    log_msg("Font loaded OK");
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
static int textWidth(TTF_Font* font, const std::string& text) {
    if (!font || text.empty()) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return w;
}
static std::vector<std::string> wrapText(TTF_Font* font, const std::string& text, int maxWidth) {
    std::vector<std::string> out;
    if (!font || text.empty()) return out;
    std::vector<std::string> paragraphs;
    std::string cur;
    for (size_t i = 0; i < text.size(); ) {
        if (i + 1 < text.size() && text[i] == '|' && text[i+1] == '|') {
            paragraphs.push_back(cur); cur.clear(); i += 2;
        } else if (text[i] == '\n') {
            paragraphs.push_back(cur); cur.clear(); ++i;
        } else { cur.push_back(text[i]); ++i; }
    }
    if (!cur.empty()) paragraphs.push_back(cur);
    for (auto& p : paragraphs) {
        std::string line, word;
        auto flush_word = [&]() {
            if (word.empty()) return;
            std::string test = line.empty() ? word : (line + " " + word);
            if (textWidth(font, test) <= maxWidth) line = test;
            else { if (!line.empty()) out.push_back(line); line = word; }
            word.clear();
        };
        for (char c : p) { if (c == ' ' || c == '\t') flush_word(); else word.push_back(c); }
        flush_word();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// ==================================================================
//                     СОХРАНЕНИЕ PNG / GIF / PCX
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
//                     КНОПКА (переиспользуемый хелпер)
// ==================================================================
struct Button {
    SDL_Rect rect;
    std::string label;
    bool hover = false;
};

static bool point_in(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void drawButton(SDL_Renderer* r, const Button& b,
                       SDL_Color bg, SDL_Color border, SDL_Color text) {
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &b.rect);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &b.rect);
    int tw = textWidth(g_ui_font, b.label);
    renderText(r, g_ui_font, b.label,
               b.rect.x + (b.rect.w - tw) / 2,
               b.rect.y + (b.rect.h - 16) / 2 - 1,
               text);
}

// ==================================================================
//                     РАЗМЕРЫ ЭЛЕМЕНТОВ
// ==================================================================
static const int TOOLBAR_H   = 48;
static const int SIDEBAR_W   = 220;
static const int PREVIEW_H   = 140;
static const int STATUSBAR_H = 24;
static const int INPUT_H     = 30;

// ==================================================================
//                     ОТРИСОВКА ЛИСТА
// ==================================================================
static void drawSheet(SDL_Renderer* r, int cx, int cy, int cw, int ch) {
    // Фон под лист
    SDL_SetRenderDrawColor(r, 16, 18, 26, 255);
    SDL_Rect bg = { cx, cy, cw, ch };
    SDL_RenderFillRect(r, &bg);

    if (!g_font.tex || !g_font.surf) {
        SDL_Color dim = { 120, 130, 150, 255 };
        int tw = textWidth(g_ui_font, S().no_image);
        renderText(r, g_ui_font, S().no_image, cx + (cw - tw) / 2, cy + ch / 2 - 8, dim);
        return;
    }

    SDL_Rect clip = { cx, cy, cw, ch };
    SDL_RenderSetClipRect(r, &clip);

    int scale = g_font.renderScale;
    int sw = g_font.surf->w * scale;
    int sh = g_font.surf->h * scale;

    // Ограничение прокрутки
    int maxSX = std::max(0, sw - cw);
    int maxSY = std::max(0, sh - ch);
    if (g_scroll_x > maxSX) g_scroll_x = maxSX;
    if (g_scroll_y > maxSY) g_scroll_y = maxSY;
    if (g_scroll_x < 0) g_scroll_x = 0;
    if (g_scroll_y < 0) g_scroll_y = 0;

    int drawX = cx - g_scroll_x;
    int drawY = cy - g_scroll_y;

    // Шахматка
    int tile = 8;
    int startX = drawX;
    int startY = drawY;
    for (int y = 0; y < sh; y += tile) {
        for (int x = 0; x < sw; x += tile) {
            int px = startX + x;
            int py = startY + y;
            if (px + tile < cx || px > cx + cw) continue;
            if (py + tile < cy || py > cy + ch) continue;
            bool dark = ((x / tile) + (y / tile)) % 2 == 0;
            SDL_SetRenderDrawColor(r, dark ? 40 : 55, dark ? 40 : 55, dark ? 50 : 65, 255);
            SDL_Rect rc = { px, py, std::min(tile, sw - x), std::min(tile, sh - y) };
            SDL_RenderFillRect(r, &rc);
        }
    }

    // Лист
    SDL_Rect dst = { drawX, drawY, sw, sh };
    SDL_RenderCopy(r, g_font.tex, nullptr, &dst);

    // Сетка символов
    int cw_ = g_font.charW * scale;
    int ch_ = g_font.charH * scale;
    SDL_SetRenderDrawColor(r, 90, 130, 190, 170);
    if (cw_ > 0) {
        int start = (g_scroll_x / cw_) * cw_;
        for (int x = start; x <= sw; x += cw_) {
            int px = drawX + x;
            if (px < cx || px > cx + cw) continue;
            SDL_RenderDrawLine(r, px, std::max(cy, drawY),
                                  px, std::min(cy + ch, drawY + sh));
        }
    }
    if (ch_ > 0) {
        int start = (g_scroll_y / ch_) * ch_;
        for (int y = start; y <= sh; y += ch_) {
            int py = drawY + y;
            if (py < cy || py > cy + ch) continue;
            SDL_RenderDrawLine(r, std::max(cx, drawX), py,
                                  std::min(cx + cw, drawX + sw), py);
        }
    }

    // Hover-подсветка ячейки
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    g_hover_valid = false;
    if (mx >= cx && mx < cx + cw && my >= cy && my < cy + ch && cw_ > 0 && ch_ > 0) {
        int fx = (mx - drawX);
        int fy = (my - drawY);
        if (fx >= 0 && fy >= 0) {
            int col = fx / cw_;
            int row = fy / ch_;
            if (col < g_font.cols && row < g_font.rows) {
                g_hover_col = col;
                g_hover_row = row;
                g_hover_valid = true;
                SDL_Rect hl = { drawX + col * cw_, drawY + row * ch_, cw_, ch_ };
                SDL_SetRenderDrawColor(r, 255, 210, 60, 255);
                SDL_RenderDrawRect(r, &hl);
                SDL_SetRenderDrawColor(r, 255, 210, 60, 60);
                SDL_Rect hl2 = { hl.x + 1, hl.y + 1, hl.w - 2, hl.h - 2 };
                SDL_RenderFillRect(r, &hl2);
            }
        }
    }

    // Рамка листа
    SDL_SetRenderDrawColor(r, 100, 140, 200, 255);
    SDL_Rect border = { drawX - 1, drawY - 1, sw + 2, sh + 2 };
    SDL_RenderDrawRect(r, &border);

    SDL_RenderSetClipRect(r, nullptr);

    // Скроллбар по вертикали
    if (sh > ch) {
        int trackX = cx + cw - 6;
        int trackY = cy;
        int trackH = ch;
        SDL_SetRenderDrawColor(r, 30, 34, 46, 200);
        SDL_Rect track = { trackX, trackY, 4, trackH };
        SDL_RenderFillRect(r, &track);
        float ratio = (float)ch / sh;
        int barH = std::max(20, (int)(trackH * ratio));
        int barY = trackY + (int)((float)g_scroll_y / sh * trackH);
        if (barY + barH > trackY + trackH) barY = trackY + trackH - barH;
        SDL_SetRenderDrawColor(r, 90, 130, 190, 220);
        SDL_Rect bar = { trackX, barY, 4, barH };
        SDL_RenderFillRect(r, &bar);
    }
    // Скроллбар горизонтальный
    if (sw > cw) {
        int trackY = cy + ch - 6;
        int trackX = cx;
        int trackW = cw;
        SDL_SetRenderDrawColor(r, 30, 34, 46, 200);
        SDL_Rect track = { trackX, trackY, trackW, 4 };
        SDL_RenderFillRect(r, &track);
        float ratio = (float)cw / sw;
        int barW = std::max(20, (int)(trackW * ratio));
        int barX = trackX + (int)((float)g_scroll_x / sw * trackW);
        if (barX + barW > trackX + trackW) barX = trackX + trackW - barW;
        SDL_SetRenderDrawColor(r, 90, 130, 190, 220);
        SDL_Rect bar = { barX, trackY, barW, 4 };
        SDL_RenderFillRect(r, &bar);
    }
}

// ==================================================================
//                     ПРЕДПРОСМОТР ТЕКСТА
// ==================================================================
static void drawPreviewText(SDL_Renderer* r, int x, int y, int maxW, int maxH) {
    if (!g_font.tex || !g_font.surf) return;
    int cw = g_font.charW, ch = g_font.charH;
    if (cw <= 0 || ch <= 0) return;
    int cols = g_font.cols, rows = g_font.rows;
    if (cols <= 0 || rows <= 0) return;

    int scale = 3;
    SDL_Rect clip = { x, y, maxW, maxH };
    SDL_RenderSetClipRect(r, &clip);

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
        if (lineX + cw * scale > x + maxW) { lineX = x; penY += ch * scale + 4; }
        if (penY > y + maxH) break;
    }

    SDL_RenderSetClipRect(r, nullptr);
}

// ==================================================================
//                     ГЛАВНАЯ ОТРИСОВКА (обычный режим)
// ==================================================================
static void drawMainUI() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);

    // === ФОН ===
    SDL_SetRenderDrawColor(g_renderer, 18, 20, 28, 255);
    SDL_RenderClear(g_renderer);

    // === ТУЛБАР ===
    SDL_SetRenderDrawColor(g_renderer, 26, 30, 42, 255);
    SDL_Rect tb = { 0, 0, w, TOOLBAR_H };
    SDL_RenderFillRect(g_renderer, &tb);
    SDL_SetRenderDrawColor(g_renderer, 50, 70, 110, 255);
    SDL_RenderDrawLine(g_renderer, 0, TOOLBAR_H - 1, w, TOOLBAR_H - 1);

    SDL_Color btnBg = { 44, 52, 72, 255 };
    SDL_Color btnBorder = { 90, 120, 180, 255 };
    SDL_Color btnText = { 225, 235, 250, 255 };

    int bx = 8;
    int bh = 34;
    int by = (TOOLBAR_H - bh) / 2;

    // Open
    int bw = textWidth(g_ui_font, S().btn_open) + 24;
    Button bOpen = { { bx, by, bw, bh }, S().btn_open, false };
    drawButton(g_renderer, bOpen, btnBg, btnBorder, btnText);
    bx += bw + 6;

    // Save PNG
    bw = textWidth(g_ui_font, S().btn_save_png) + 24;
    Button bPNG = { { bx, by, bw, bh }, S().btn_save_png, false };
    drawButton(g_renderer, bPNG, btnBg, btnBorder, btnText);
    bx += bw + 4;

    // Save GIF
    bw = textWidth(g_ui_font, S().btn_save_gif) + 24;
    Button bGIF = { { bx, by, bw, bh }, S().btn_save_gif, false };
    drawButton(g_renderer, bGIF, btnBg, btnBorder, btnText);
    bx += bw + 4;

    // Save PCX
    bw = textWidth(g_ui_font, S().btn_save_pcx) + 24;
    Button bPCX = { { bx, by, bw, bh }, S().btn_save_pcx, false };
    drawButton(g_renderer, bPCX, btnBg, btnBorder, btnText);
    bx += bw + 16;

    // Разделитель
    SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
    SDL_RenderDrawLine(g_renderer, bx, 10, bx, TOOLBAR_H - 10);
    bx += 12;

    // Paint
    bw = textWidth(g_ui_font, S().btn_paint) + 24;
    Button bPaint = { { bx, by, bw, bh }, S().btn_paint, false };
    drawButton(g_renderer, bPaint, btnBg, btnBorder, btnText);
    bx += bw + 6;

    // Help
    bw = textWidth(g_ui_font, S().btn_help) + 24;
    Button bHelp = { { bx, by, bw, bh }, S().btn_help, false };
    drawButton(g_renderer, bHelp, btnBg, btnBorder, btnText);

    // Language (справа)
    int langW = textWidth(g_ui_font, S().btn_lang) + 24;
    Button bLang = { { w - langW - 8, by, langW, bh }, S().btn_lang, false };
    drawButton(g_renderer, bLang, { 60, 80, 130, 255 }, btnBorder, btnText);

    // === БОКОВАЯ ПАНЕЛЬ НАСТРОЕК ===
    int sbX = w - SIDEBAR_W;
    SDL_SetRenderDrawColor(g_renderer, 24, 28, 40, 255);
    SDL_Rect sb = { sbX, TOOLBAR_H, SIDEBAR_W, h - TOOLBAR_H - STATUSBAR_H };
    SDL_RenderFillRect(g_renderer, &sb);
    SDL_SetRenderDrawColor(g_renderer, 50, 70, 110, 255);
    SDL_RenderDrawLine(g_renderer, sbX, TOOLBAR_H, sbX, h - STATUSBAR_H);

    TTF_Font* fb = g_ui_font_bold ? g_ui_font_bold : g_ui_font;
    renderText(g_renderer, fb, S().settings_title, sbX + 16, TOOLBAR_H + 14, SDL_Color{150,190,240,255});

    struct SetRow { const char* label; int* val; int minv; int maxv; };
    SetRow rows[5] = {
        { S().set_charw.c_str(),   &g_font.charW,      1,  64 },
        { S().set_charh.c_str(),   &g_font.charH,      1,  64 },
        { S().set_first.c_str(),   &g_font.firstChar,  0, 255 },
        { S().set_spacing.c_str(), &g_font.spacing,  -10,  32 },
        { S().set_scale.c_str(),   &g_font.renderScale, 1,   8 },
    };

    int rowY = TOOLBAR_H + 46;
    int rowH = 42;
    for (int i = 0; i < 5; ++i) {
        int rx = sbX + 16;
        int rw = SIDEBAR_W - 32;
        // Заголовок
        renderText(g_renderer, g_ui_font, rows[i].label, rx, rowY, SDL_Color{180,200,230,255});

        // Кнопки [-][value][+]
        int minusX = rx;
        int plusX  = rx + rw - 30;
        int valX   = rx + 34;
        int valW   = rw - 34 - 34;
        int btnY   = rowY + 20;
        int btnS   = 26;

        SDL_Rect rm = { minusX, btnY, btnS, btnS };
        SDL_Rect rp = { plusX, btnY, btnS, btnS };
        SDL_Rect rv = { valX, btnY, valW, btnS };

        SDL_Color c1 = { 44, 52, 72, 255 };
        SDL_Color c2 = { 90, 120, 180, 255 };
        SDL_Color ct = { 220, 230, 250, 255 };

        drawButton(g_renderer, { rm, "−", false }, c1, c2, ct);
        drawButton(g_renderer, { rp, "+", false }, c1, c2, ct);

        // Значение
        SDL_SetRenderDrawColor(g_renderer, 14, 18, 28, 255);
        SDL_RenderFillRect(g_renderer, &rv);
        SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
        SDL_RenderDrawRect(g_renderer, &rv);
        std::string vs = std::to_string(*rows[i].val);
        int vw = textWidth(g_ui_font, vs);
        renderText(g_renderer, g_ui_font, vs, rv.x + (rv.w - vw) / 2, rv.y + 5, ct);

        rowY += rowH;
    }

    // Кнопка сброса
    int resetX = sbX + 16;
    int resetY = rowY + 6;
    int resetW = SIDEBAR_W - 32;
    int resetH = 32;
    Button bReset = { { resetX, resetY, resetW, resetH }, S().reset_defaults, false };
    drawButton(g_renderer, bReset, { 90, 60, 60, 255 }, { 180, 100, 100, 255 }, btnText);

    // === ОБЛАСТЬ ЛИСТА ===
    int sheetX = 0;
    int sheetY = TOOLBAR_H;
    int sheetW = w - SIDEBAR_W;
    int sheetH = h - TOOLBAR_H - PREVIEW_H - STATUSBAR_H;
    drawSheet(g_renderer, sheetX, sheetY, sheetW, sheetH);

    // === ОБЛАСТЬ ПРЕДПРОСМОТРА ===
    int pvX = 0;
    int pvY = h - STATUSBAR_H - PREVIEW_H;
    int pvW = w - SIDEBAR_W;
    int pvH = PREVIEW_H;
    SDL_SetRenderDrawColor(g_renderer, 22, 26, 36, 255);
    SDL_Rect pv = { pvX, pvY, pvW, pvH };
    SDL_RenderFillRect(g_renderer, &pv);
    SDL_SetRenderDrawColor(g_renderer, 50, 70, 110, 255);
    SDL_RenderDrawLine(g_renderer, pvX, pvY, pvX + pvW, pvY);

    renderText(g_renderer, fb, S().preview_title, pvX + 16, pvY + 10, SDL_Color{150,190,240,255});

    // Поле ввода
    int inputX = pvX + 16;
    int inputY = pvY + 34;
    int inputW = pvW - 32;
    SDL_Rect inputRect = { inputX, inputY, inputW, INPUT_H };
    SDL_SetRenderDrawColor(g_renderer, 12, 16, 24, 255);
    SDL_RenderFillRect(g_renderer, &inputRect);
    SDL_SetRenderDrawColor(g_renderer,
        g_typing ? 120 : 60, g_typing ? 180 : 80, g_typing ? 240 : 120, 255);
    SDL_RenderDrawRect(g_renderer, &inputRect);

    SDL_Color txtColor = { 230, 240, 255, 255 };
    if (g_test_text.empty() && !g_typing) {
        renderText(g_renderer, g_ui_font, S().input_hint,
                   inputX + 8, inputY + 7, SDL_Color{110,130,160,255});
    } else {
        renderText(g_renderer, g_ui_font, g_test_text, inputX + 8, inputY + 7, txtColor);
    }

    // Курсор
    if (g_typing && (SDL_GetTicks() / 500) % 2 == 0) {
        int tw = textWidth(g_ui_font, g_test_text);
        SDL_SetRenderDrawColor(g_renderer, 200, 230, 255, 255);
        SDL_Rect cur = { inputX + 8 + tw + 1, inputY + 7, 2, 16 };
        SDL_RenderFillRect(g_renderer, &cur);
    }

    // Рисуем текст шрифтом пользователя ниже
    drawPreviewText(g_renderer, pvX + 16, inputY + INPUT_H + 8, pvW - 32, pvH - INPUT_H - 48);

    // === СТАТУСБАР ===
    SDL_SetRenderDrawColor(g_renderer, 20, 24, 34, 255);
    SDL_Rect sbr = { 0, h - STATUSBAR_H, w, STATUSBAR_H };
    SDL_RenderFillRect(g_renderer, &sbr);
    SDL_SetRenderDrawColor(g_renderer, 50, 70, 110, 255);
    SDL_RenderDrawLine(g_renderer, 0, h - STATUSBAR_H, w, h - STATUSBAR_H);

    // Слева: путь файла
    std::string fileInfo = g_font.surf
        ? (S().status_file + ": " + g_font.path)
        : S().status_no_file;
    // Обрезаем путь
    int maxLen = 70;
    if ((int)fileInfo.size() > maxLen)
        fileInfo = "..." + fileInfo.substr(fileInfo.size() - maxLen);
    renderText(g_renderer, g_ui_font, fileInfo, 10, h - STATUSBAR_H + 4, SDL_Color{140,160,190,255});

    // По центру: hover-инфа
    if (g_hover_valid && g_font.surf) {
        int idx = g_hover_row * g_font.cols + g_hover_col;
        int code = idx + g_font.firstChar;
        char buf[128];
        snprintf(buf, sizeof(buf), "cell %d,%d  index=%d  code=%d",
                 g_hover_col, g_hover_row, idx, code);
        int tw = textWidth(g_ui_font, buf);
        renderText(g_renderer, g_ui_font, buf, (w - tw) / 2, h - STATUSBAR_H + 4,
                   SDL_Color{200,220,250,255});
    } else if (g_font.surf) {
        int tw = textWidth(g_ui_font, S().hover_none);
        renderText(g_renderer, g_ui_font, S().hover_none, (w - tw) / 2, h - STATUSBAR_H + 4,
                   SDL_Color{100,120,150,255});
    }

    // Справа: параметры
    char buf[128];
    snprintf(buf, sizeof(buf), "%dx%d  chars=%d  [%s]",
             g_font.charW, g_font.charH, g_font.total_chars, S().btn_lang.c_str());
    int tw = textWidth(g_ui_font, buf);
    renderText(g_renderer, g_ui_font, buf, w - tw - 10, h - STATUSBAR_H + 4,
               SDL_Color{140,170,210,255});
}

// ==================================================================
//                     HELP WINDOW
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

    SDL_SetRenderDrawColor(g_renderer, 40, 60, 100, 255);
    SDL_Rect hdr = { dx, dy, dw, 40 };
    SDL_RenderFillRect(g_renderer, &hdr);
    TTF_Font* fb = g_ui_font_bold ? g_ui_font_bold : g_ui_font;
    renderText(g_renderer, fb, S().help_title, dx + 16, dy + 10, SDL_Color{200,220,255,255});

    int contentX = dx + 20;
    int contentY = dy + 54;
    int contentW = dw - 40;
    int contentH = dh - 54 - 60;

    SDL_Rect clipRect = { contentX, contentY, contentW, contentH };
    SDL_RenderSetClipRect(g_renderer, &clipRect);

    int lineH = 20;
    int y = contentY;

    renderText(g_renderer, fb, S().help_hotkeys, contentX, y, SDL_Color{140,180,240,255});
    y += 28;

    const char* keysEN[] = {
        "Ctrl+O", "Ctrl+S", "Ctrl+Shift+S", "Ctrl+Alt+S",
        "F1 / F2", "F3 / F4", "F5 / F6", "F7 / F8", "F9 / F10",
        "L", "T", "E", "H", "Esc"
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
        "Focus text input (bottom)",
        "Paint editor",
        "Help / About (this window)",
        "Exit program / close dialog"
    };
    const char* keysRU[] = {
        "Ctrl+O", "Ctrl+S", "Ctrl+Shift+S", "Ctrl+Alt+S",
        "F1 / F2", "F3 / F4", "F5 / F6", "F7 / F8", "F9 / F10",
        "L", "T", "E", "H", "Esc"
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
        "Фокус на вводе текста внизу",
        "Редактор (Paint)",
        "Справка / О программе (это окно)",
        "Выход / закрыть окно"
    };
    const char** keys = (g_lang == LANG_RU) ? keysRU : keysEN;
    const char** desc = (g_lang == LANG_RU) ? descRU : descEN;

    int keyColW = 180;
    for (int i = 0; i < 14; ++i) {
        renderText(g_renderer, g_ui_font, keys[i], contentX, y, SDL_Color{240,220,120,255});
        renderText(g_renderer, g_ui_font, desc[i], contentX + keyColW, y, SDL_Color{220,230,250,255});
        y += lineH;
    }

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

    int btnW = 200, btnH = 34;
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

    SDL_SetRenderDrawColor(g_renderer, 120, 170, 240, 255);
    SDL_RenderDrawRect(g_renderer, &dlg);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

// ==================================================================
//                     PAINT
// ==================================================================
struct RGB { Uint8 r, g, b; };
static const RGB PALETTE[24] = {
    {0,0,0}, {64,64,64}, {128,128,128}, {192,192,192},
    {255,255,255}, {128,0,0}, {200,0,0}, {255,80,80},
    {255,128,0}, {255,200,0}, {255,255,0}, {128,128,0},
    {0,128,0}, {0,200,80}, {0,255,128}, {0,200,200},
    {0,128,255}, {0,0,255}, {64,0,160}, {128,0,255},
    {200,0,200}, {255,0,255}, {255,0,128}, {128,64,32}
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

    int topH = 48, palW = 180;
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
    int bheight = 34;
    int by = (topH - bheight) / 2;
    for (int i = 0; i < 4; ++i) {
        int bw = textWidth(g_ui_font, toolNames[i]) + 24;
        SDL_Rect btn = { bx, by, bw, bheight };
        if (i == g_paint.tool) SDL_SetRenderDrawColor(g_renderer, 60, 100, 180, 255);
        else                   SDL_SetRenderDrawColor(g_renderer, 44, 52, 72, 255);
        SDL_RenderFillRect(g_renderer, &btn);
        SDL_SetRenderDrawColor(g_renderer, 90, 120, 180, 255);
        SDL_RenderDrawRect(g_renderer, &btn);
        int tw = textWidth(g_ui_font, toolNames[i]);
        renderText(g_renderer, g_ui_font, toolNames[i], bx + (bw - tw)/2, by + 9, txt);
        bx += bw + 6;
    }
    bx += 16;
    renderText(g_renderer, g_ui_font, "Zoom:", bx, by + 8, dim);
    bx += textWidth(g_ui_font, "Zoom:") + 8;
    renderText(g_renderer, g_ui_font, "x" + std::to_string(g_paint.zoom), bx, by + 8, txt);

    int cancelW = textWidth(g_ui_font, S().cancel) + 24;
    int applyW  = textWidth(g_ui_font, S().apply)  + 24;
    int cancelX = w - cancelW - 8;
    int applyX  = cancelX - applyW - 6;
    SDL_Rect btnCancel = { cancelX, by, cancelW, bheight };
    SDL_Rect btnApply  = { applyX,  by, applyW,  bheight };
    SDL_SetRenderDrawColor(g_renderer, 140, 60, 60, 255);
    SDL_RenderFillRect(g_renderer, &btnCancel);
    SDL_SetRenderDrawColor(g_renderer, 200, 100, 100, 255);
    SDL_RenderDrawRect(g_renderer, &btnCancel);
    { int tw = textWidth(g_ui_font, S().cancel);
      renderText(g_renderer, g_ui_font, S().cancel, cancelX + (cancelW - tw)/2, by + 9, txt); }
    SDL_SetRenderDrawColor(g_renderer, 60, 130, 70, 255);
    SDL_RenderFillRect(g_renderer, &btnApply);
    SDL_SetRenderDrawColor(g_renderer, 110, 210, 120, 255);
    SDL_RenderDrawRect(g_renderer, &btnApply);
    { int tw = textWidth(g_ui_font, S().apply);
      renderText(g_renderer, g_ui_font, S().apply, applyX + (applyW - tw)/2, by + 9, txt); }

    int palX = w - palW;
    SDL_SetRenderDrawColor(g_renderer, 26, 30, 42, 255);
    SDL_Rect palBg = { palX, topH, palW, h - topH };
    SDL_RenderFillRect(g_renderer, &palBg);
    SDL_SetRenderDrawColor(g_renderer, 60, 80, 120, 255);
    SDL_RenderDrawLine(g_renderer, palX, topH, palX, h);

    int swatch = 32, gap = 8;
    int px0 = palX + (palW - (4 * swatch + 3 * gap)) / 2;
    int py0 = topH + 20;
    for (int i = 0; i < 24; ++i) {
        int col = i % 4, row = i / 4;
        SDL_Rect rc = { px0 + col * (swatch + gap), py0 + row * (swatch + gap), swatch, swatch };
        SDL_SetRenderDrawColor(g_renderer, PALETTE[i].r, PALETTE[i].g, PALETTE[i].b, 255);
        SDL_RenderFillRect(g_renderer, &rc);
        bool is_cur = g_paint.color[0] == PALETTE[i].r && g_paint.color[1] == PALETTE[i].g &&
                      g_paint.color[2] == PALETTE[i].b && g_paint.color[3] == 255;
        if (is_cur) {
            SDL_SetRenderDrawColor(g_renderer, 255, 220, 80, 255);
            SDL_Rect border = { rc.x - 2, rc.y - 2, rc.w + 4, rc.h + 4 };
            SDL_RenderDrawRect(g_renderer, &border);
        }
    }
    int tyP = py0 + 6 * (swatch + gap) + 8;
    SDL_Rect transp = { palX + 12, tyP, palW - 24, 32 };
    bool is_tr = g_paint.color[3] == 0;
    SDL_SetRenderDrawColor(g_renderer, is_tr?70:40, is_tr?110:50, is_tr?190:70, 255);
    SDL_RenderFillRect(g_renderer, &transp);
    SDL_SetRenderDrawColor(g_renderer, 120, 160, 220, 255);
    SDL_RenderDrawRect(g_renderer, &transp);
    { int tw = textWidth(g_ui_font, S().transparent);
      renderText(g_renderer, g_ui_font, S().transparent, palX + (palW - tw)/2, tyP + 8, txt); }
    SDL_Rect undoBtn = { palX + 12, tyP + 40, palW - 24, 32 };
    SDL_SetRenderDrawColor(g_renderer, 55, 62, 82, 255);
    SDL_RenderFillRect(g_renderer, &undoBtn);
    SDL_SetRenderDrawColor(g_renderer, 120, 160, 220, 255);
    SDL_RenderDrawRect(g_renderer, &undoBtn);
    { int tw = textWidth(g_ui_font, S().undo);
      renderText(g_renderer, g_ui_font, S().undo, palX + (palW - tw)/2, tyP + 48, txt); }

    // Нижняя панель Paint
    SDL_SetRenderDrawColor(g_renderer, 20, 24, 34, 255);
    SDL_Rect bot = { 0, h - 24, w, 24 };
    SDL_RenderFillRect(g_renderer, &bot);
    renderText(g_renderer, g_ui_font, S().paint_hint, 10, h - 20, dim);
}

// ==================================================================
//                     ДИСПЕТЧЕР
// ==================================================================
static void drawUI() {
    if (g_paint.active) drawPaintEditor();
    else                drawMainUI();
    if (g_help.active)  drawHelp();
    SDL_RenderPresent(g_renderer);
}

// ==================================================================
//                     UI-ШРИФТ
// ==================================================================
static TTF_Font* loadUIFont(int size) {
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/consola.ttf",
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
    int availW = w - 180 - 40;
    int availH = h - 48 - 24 - 40;
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
//                     КЛИКИ ПО UI
// ==================================================================
static bool handleMainUIClick(int mx, int my) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);

    SDL_Color btnBg = { 44, 52, 72, 255 };

    int bx = 8;
    int bh = 34;
    int by = (TOOLBAR_H - bh) / 2;

    // Open
    int bw = textWidth(g_ui_font, S().btn_open) + 24;
    if (point_in({ bx, by, bw, bh }, mx, my)) {
        std::string p = openFileDialogW();
        if (!p.empty()) loadFont(p);
        return true;
    }
    bx += bw + 6;

    // Save PNG
    bw = textWidth(g_ui_font, S().btn_save_png) + 24;
    if (point_in({ bx, by, bw, bh }, mx, my)) {
        if (g_font.surf) {
            std::string p = saveFileDialogW("png", L"Indexed PNG\0*.png\0All Files\0*.*\0\0");
            if (!p.empty() && savePNG_Indexed(g_font.surf, p.c_str())) log_msg("Saved PNG: " + p);
        }
        return true;
    }
    bx += bw + 4;

    // Save GIF
    bw = textWidth(g_ui_font, S().btn_save_gif) + 24;
    if (point_in({ bx, by, bw, bh }, mx, my)) {
        if (g_font.surf) {
            std::string p = saveFileDialogW("gif", L"GIF image\0*.gif\0All Files\0*.*\0\0");
            if (!p.empty() && saveGIF(g_font.surf, p.c_str())) log_msg("Saved GIF: " + p);
        }
        return true;
    }
    bx += bw + 4;

    // Save PCX
    bw = textWidth(g_ui_font, S().btn_save_pcx) + 24;
    if (point_in({ bx, by, bw, bh }, mx, my)) {
        if (g_font.surf) {
            std::string p = saveFileDialogW("pcx", L"PCX image\0*.pcx\0All Files\0*.*\0\0");
            if (!p.empty() && savePCX(g_font.surf, p.c_str())) log_msg("Saved PCX: " + p);
        }
        return true;
    }
    bx += bw + 16 + 12;

    // Paint
    bw = textWidth(g_ui_font, S().btn_paint) + 24;
    if (point_in({ bx, by, bw, bh }, mx, my)) { enterPaint(); return true; }
    bx += bw + 6;

    // Help
    bw = textWidth(g_ui_font, S().btn_help) + 24;
    if (point_in({ bx, by, bw, bh }, mx, my)) { g_help.active = true; return true; }

    // Lang
    int langW = textWidth(g_ui_font, S().btn_lang) + 24;
    if (point_in({ w - langW - 8, by, langW, bh }, mx, my)) {
        g_lang = (g_lang == LANG_EN) ? LANG_RU : LANG_EN;
        if (g_window) SDL_SetWindowTitle(g_window, S().title.c_str());
        return true;
    }

    // Sidebar: rows +/-
    int sbX = w - SIDEBAR_W;
    if (mx >= sbX) {
        int rowY = TOOLBAR_H + 46;
        int rowH = 42;
        struct Tgt { int* val; int minv; int maxv; bool recalc; };
        Tgt tgts[5] = {
            { &g_font.charW, 1, 64, true },
            { &g_font.charH, 1, 64, true },
            { &g_font.firstChar, 0, 255, false },
            { &g_font.spacing, -10, 32, false },
            { &g_font.renderScale, 1, 8, false },
        };
        for (int i = 0; i < 5; ++i) {
            int rx = sbX + 16;
            int rw = SIDEBAR_W - 32;
            int minusX = rx;
            int plusX  = rx + rw - 30;
            int btnY   = rowY + 20;
            int btnS   = 26;
            if (point_in({ minusX, btnY, btnS, btnS }, mx, my)) {
                *tgts[i].val = std::max(tgts[i].minv, *tgts[i].val - 1);
                if (tgts[i].recalc) refreshCharCount();
                return true;
            }
            if (point_in({ plusX, btnY, btnS, btnS }, mx, my)) {
                *tgts[i].val = std::min(tgts[i].maxv, *tgts[i].val + 1);
                if (tgts[i].recalc) refreshCharCount();
                return true;
            }
            rowY += rowH;
        }
        // Reset
        int resetY = rowY + 6;
        if (point_in({ sbX + 16, resetY, SIDEBAR_W - 32, 32 }, mx, my)) {
            g_font.charW = 8; g_font.charH = 8;
            g_font.firstChar = 0; g_font.spacing = 0;
            g_font.renderScale = 3;
            refreshCharCount();
            return true;
        }
        return true;
    }

    // Клик по области предпросмотра: включить ввод
    int pvY = h - STATUSBAR_H - PREVIEW_H;
    if (my >= pvY && my < h - STATUSBAR_H) {
        int inputX = 16;
        int inputY = pvY + 34;
        int inputW = w - SIDEBAR_W - 32;
        if (point_in({ inputX, inputY, inputW, INPUT_H }, mx, my)) {
            g_typing = true;
            SDL_StartTextInput();
            return true;
        }
    }

    return false;
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
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1200, 760,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_window) { log_msg("CreateWindow failed"); return 1; }
    SDL_SetWindowMinimumSize(g_window, 800, 600);
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) { log_msg("CreateRenderer failed"); return 1; }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    {
        SDL_Surface* icon = IMG_Load("icon.png");
        if (icon) { SDL_SetWindowIcon(g_window, icon); SDL_FreeSurface(icon); log_msg("Icon loaded"); }
    }

    g_ui_font = loadUIFont(14);
    g_ui_font_bold = loadUIFont(16);

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
                    int dw = std::min(w - 60, 900);
                    int dh = std::min(h - 60, 620);
                    int dx = (w - dw) / 2;
                    int dy = (h - dh) / 2;
                    int btnW = 200, btnH = 34;
                    int btnX = dx + (dw - btnW) / 2;
                    int btnY = dy + dh - 48;
                    if (point_in({ btnX, btnY, btnW, btnH }, e.button.x, e.button.y)) {
                        g_help.active = false;
                    } else if (e.button.x < dx || e.button.x > dx + dw ||
                               e.button.y < dy || e.button.y > dy + dh) {
                        g_help.active = false;
                    }
                }
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
                    int palW = 180;
                    int canvasX = 0, canvasY = 48, canvasW = w - palW, canvasH = h - 48 - 24;
                    int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
                    int my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;

                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && my < 48) {
                        const char* toolNames[4] = { S().tool_pencil.c_str(), S().tool_eraser.c_str(),
                                                     S().tool_fill.c_str(), S().tool_picker.c_str() };
                        int bx = 8; bool handled = false;
                        int bh = 34, by = (48 - bh) / 2;
                        for (int i = 0; i < 4; ++i) {
                            int bw = textWidth(g_ui_font, toolNames[i]) + 24;
                            if (point_in({ bx, by, bw, bh }, mx, my)) { g_paint.tool = i; handled = true; break; }
                            bx += bw + 6;
                        }
                        if (!handled) {
                            int cancelW = textWidth(g_ui_font, S().cancel) + 24;
                            int applyW  = textWidth(g_ui_font, S().apply)  + 24;
                            int cancelX = w - cancelW - 8;
                            int applyX  = cancelX - applyW - 6;
                            if (point_in({ cancelX, by, cancelW, bh }, mx, my)) { cancelPaint(); handled = true; }
                            else if (point_in({ applyX, by, applyW, bh }, mx, my)) { applyPaint(); handled = true; }
                        }
                    }
                    else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                             mx >= w - palW && my >= 48) {
                        int swatch = 32, gap = 8;
                        int px0 = (w - palW) + (palW - (4 * swatch + 3 * gap)) / 2;
                        int py0 = 48 + 20;
                        bool handled = false;
                        for (int i = 0; i < 24; ++i) {
                            int col = i % 4, row = i / 4;
                            int sx = px0 + col * (swatch + gap);
                            int sy = py0 + row * (swatch + gap);
                            if (point_in({ sx, sy, swatch, swatch }, mx, my)) {
                                g_paint.color[0] = PALETTE[i].r;
                                g_paint.color[1] = PALETTE[i].g;
                                g_paint.color[2] = PALETTE[i].b;
                                g_paint.color[3] = 255;
                                handled = true; break;
                            }
                        }
                        if (!handled) {
                            int tyP = py0 + 6 * (swatch + gap) + 8;
                            if (point_in({ (w - palW) + 12, tyP, palW - 24, 32 }, mx, my)) {
                                g_paint.color[3] = 0;
                            }
                            if (point_in({ (w - palW) + 12, tyP + 40, palW - 24, 32 }, mx, my)) {
                                paintUndo();
                            }
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

            // ---- MAIN UI ----
            if (e.type == SDL_DROPFILE) {
                std::string path = e.drop.file ? e.drop.file : "";
                if (e.drop.file) SDL_free(e.drop.file);
                log_msg("DnD: " + path);
                loadFont(path);
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                // Клик по листу фокусирует его и снимает фокус с текстового поля?
                // Оставляем фокус на поле ввода до нажатия Escape
                handleMainUIClick(e.button.x, e.button.y);
            }
            else if (e.type == SDL_MOUSEWHEEL) {
                int w, h; SDL_GetRendererOutputSize(g_renderer, &w, &h);
                int mx, my; SDL_GetMouseState(&mx, &my);
                int sheetH = h - TOOLBAR_H - PREVIEW_H - STATUSBAR_H;
                if (mx < w - SIDEBAR_W && my >= TOOLBAR_H && my < TOOLBAR_H + sheetH) {
                    if (e.wheel.y != 0) {
                        if (SDL_GetModState() & KMOD_SHIFT) {
                            g_scroll_x -= e.wheel.y * 32;
                        } else {
                            g_scroll_y -= e.wheel.y * 32;
                        }
                    }
                }
            }
            else if (e.type == SDL_TEXTINPUT) {
                if (g_typing) g_test_text += e.text.text;
            }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                SDL_Keymod mod = (SDL_Keymod)e.key.keysym.mod;

                // Если идёт ввод текста — обрабатываем только ввод
                if (g_typing) {
                    if (k == SDLK_ESCAPE) { g_typing = false; SDL_StopTextInput(); }
                    else if (k == SDLK_BACKSPACE) utf8_pop_back(g_test_text);
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        g_typing = false;
                        SDL_StopTextInput();
                    }
                    continue;
                }

                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_h) { g_help.active = true; }
                else if (k == SDLK_t) {
                    g_typing = true;
                    SDL_StartTextInput();
                }
                else if (k == SDLK_o && (mod & KMOD_CTRL)) {
                    std::string path = openFileDialogW();
                    if (!path.empty()) loadFont(path);
                }
                else if (k == SDLK_s && (mod & KMOD_CTRL)) {
                    if (g_font.surf) {
                        std::string path;
                        if (mod & KMOD_SHIFT) {
                            path = saveFileDialogW("gif", L"GIF image\0*.gif\0All Files\0*.*\0\0");
                            if (!path.empty()) saveGIF(g_font.surf, path.c_str());
                        } else if (mod & KMOD_ALT) {
                            path = saveFileDialogW("pcx", L"PCX image\0*.pcx\0All Files\0*.*\0\0");
                            if (!path.empty()) savePCX(g_font.surf, path.c_str());
                        } else {
                            path = saveFileDialogW("png", L"Indexed PNG\0*.png\0All Files\0*.*\0\0");
                            if (!path.empty()) savePNG_Indexed(g_font.surf, path.c_str());
                        }
                    }
                }
                else if (k == SDLK_l) {
                    g_lang = (g_lang == LANG_EN) ? LANG_RU : LANG_EN;
                    if (g_window) SDL_SetWindowTitle(g_window, S().title.c_str());
                }
                else if (k == SDLK_e) enterPaint();
                else if (k == SDLK_F1)  { g_font.charW = std::max(1, g_font.charW - 1); refreshCharCount(); }
                else if (k == SDLK_F2)  { g_font.charW = std::min(64, g_font.charW + 1); refreshCharCount(); }
                else if (k == SDLK_F3)  { g_font.charH = std::max(1, g_font.charH - 1); refreshCharCount(); }
                else if (k == SDLK_F4)  { g_font.charH = std::min(64, g_font.charH + 1); refreshCharCount(); }
                else if (k == SDLK_F5)  g_font.firstChar = std::max(0, g_font.firstChar - 1);
                else if (k == SDLK_F6)  g_font.firstChar = std::min(255, g_font.firstChar + 1);
                else if (k == SDLK_F7)  g_font.renderScale = std::max(1, g_font.renderScale - 1);
                else if (k == SDLK_F8)  g_font.renderScale = std::min(8, g_font.renderScale + 1);
                else if (k == SDLK_F9)  g_font.spacing = std::max(-10, g_font.spacing - 1);
                else if (k == SDLK_F10) g_font.spacing = std::min(32, g_font.spacing + 1);
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
