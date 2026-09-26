// save_formats.h — сохранение в индексированные PNG, GIF, PCX
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <png.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// ==================================================================
//          КОНВЕРТАЦИЯ SURFACE В 8-БИТНЫЙ ИНДЕКСИРОВАННЫЙ
// ==================================================================
// Возвращает новый SDL_Surface в формате SDL_PIXELFORMAT_INDEX8
// с палитрой из исходного изображения. Исходный surface НЕ удаляется.
static SDL_Surface* toIndexed8(SDL_Surface* src) {
    if (!src) return nullptr;

    // Создаём 8-битную палитровую поверхность с той же палитрой
    SDL_Palette* pal = src->format->palette;
    if (!pal) {
        // Если палитры нет — сначала конвертируем в ARGB32, потом соберём палитру
        SDL_Surface* tmp = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!tmp) return nullptr;

        // Собираем уникальные цвета
        std::vector<uint32_t> colors;
        SDL_LockSurface(tmp);
        uint32_t* px = (uint32_t*)tmp->pixels;
        int n = tmp->w * tmp->h;
        for (int i = 0; i < n; ++i) {
            uint32_t c = px[i] & 0x00FFFFFF;
            if (std::find(colors.begin(), colors.end(), c) == colors.end())
                colors.push_back(c);
            if (colors.size() >= 256) break;
        }
        SDL_UnlockSurface(tmp);

        SDL_Palette* newpal = SDL_AllocPalette((int)colors.size());
        for (size_t i = 0; i < colors.size(); ++i) {
            SDL_Color c;
            c.r = (colors[i] >> 16) & 0xFF;
            c.g = (colors[i] >> 8)  & 0xFF;
            c.b =  colors[i]        & 0xFF;
            c.a = 255;
            newpal->colors[i] = c;
        }

        SDL_Surface* indexed = SDL_CreateRGBSurfaceWithFormat(
            0, src->w, src->h, 8, SDL_PIXELFORMAT_INDEX8);
        SDL_SetSurfacePalette(indexed, newpal);

        SDL_LockSurface(tmp);
        SDL_LockSurface(indexed);
        uint32_t* src_px = (uint32_t*)tmp->pixels;
        uint8_t*  dst_px = (uint8_t*)indexed->pixels;
        for (int i = 0; i < n; ++i) {
            uint32_t c = src_px[i] & 0x00FFFFFF;
            for (size_t j = 0; j < colors.size(); ++j) {
                if (colors[j] == c) { dst_px[i] = (uint8_t)j; break; }
            }
        }
        SDL_UnlockSurface(indexed);
        SDL_UnlockSurface(tmp);

        SDL_FreeSurface(tmp);
        SDL_FreePalette(newpal);
        return indexed;
    }

    // Если палитра уже есть — просто конвертируем формат
    SDL_Surface* indexed = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_INDEX8, 0);
    return indexed;
}

// ==================================================================
//          СОХРАНЕНИЕ PNG (ИНДЕКСИРОВАННЫЙ, ЧЕРЕЗ libpng)
// ==================================================================
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

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        SDL_FreeSurface(idx);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, idx->w, idx->h, 8,
                 PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    // Палитра
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

    // Строки
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

// ==================================================================
//          СОХРАНЕНИЕ GIF (ЧЕРЕЗ IMG_SaveGIF)
// ==================================================================
static bool saveGIF(SDL_Surface* src, const char* path) {
    if (!src || !path) return false;
#ifdef IMG_SaveGIF
    return IMG_SaveGIF(src, path);
#else
    SDL_SetError("IMG_SaveGIF not available (need SDL2_image >= 3.4.0)");
    return false;
#endif
}

// ==================================================================
//          СОХРАНЕНИЕ PCX (СОБСТВЕННАЯ РЕАЛИЗАЦИЯ)
// ==================================================================
#pragma pack(push, 1)
struct PCXHeader {
    uint8_t  manufacturer;   // 10
    uint8_t  version;        // 5
    uint8_t  encoding;       // 1 = RLE
    uint8_t  bitsPerPixel;   // 8
    uint16_t xmin, ymin, xmax, ymax;
    uint16_t hres, vres;
    uint8_t  palette16[48];
    uint8_t  reserved;
    uint8_t  nplanes;        // 1 для 8-битного индексированного
    uint16_t bytesPerLine;
    uint16_t paletteInfo;    // 1 = цветная палитра
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

    // Заголовок
    PCXHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.manufacturer = 10;
    hdr.version      = 5;
    hdr.encoding     = 1;   // RLE
    hdr.bitsPerPixel = 8;
    hdr.xmin = 0;
    hdr.ymin = 0;
    hdr.xmax = (uint16_t)(idx->w - 1);
    hdr.ymax = (uint16_t)(idx->h - 1);
    hdr.hres = 300;
    hdr.vres = 300;
    hdr.nplanes = 1;
    hdr.bytesPerLine = (uint16_t)((idx->w + 1) & ~1);  // чётное
    hdr.paletteInfo  = 1;
    fwrite(&hdr, sizeof(hdr), 1, fp);

    // Данные с RLE
    SDL_LockSurface(idx);
    for (int y = 0; y < idx->h; ++y) {
        uint8_t* line = (uint8_t*)idx->pixels + y * idx->pitch;
        int x = 0;
        while (x < hdr.bytesPerLine) {
            uint8_t val = (x < idx->w) ? line[x] : 0;
            int run = 1;
            while (x + run < hdr.bytesPerLine &&
                   ((x + run < idx->w) ? line[x + run] : 0) == val &&
                   run < 63) {
                ++run;
            }
            if (run > 1 || (val & 0xC0) == 0xC0) {
                fputc(0xC0 | run, fp);
                fputc(val, fp);
            } else {
                fputc(val, fp);
            }
            x += run;
        }
    }
    SDL_UnlockSurface(idx);

    // Палитра (768 байт в конце файла, после маркера 0x0C)
    fputc(0x0C, fp);
    SDL_Palette* pal = idx->format->palette;
    for (int i = 0; i < 256; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (pal && i < pal->ncolors) {
            r = pal->colors[i].r;
            g = pal->colors[i].g;
            b = pal->colors[i].b;
        }
        fputc(r, fp);
        fputc(g, fp);
        fputc(b, fp);
    }

    fclose(fp);
    SDL_FreeSurface(idx);
    return true;
}
