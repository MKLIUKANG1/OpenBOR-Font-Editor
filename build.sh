#!/bin/bash
set -e
cd "$(dirname "$0")"

MINGW=/mingw64/bin
mkdir -p build

cp -f icon.png build/ 2>/dev/null || true

echo "[1/3] Compiling..."
# Компилируем ресурсы (иконка)
windres resources.rc -O coff -o build/resources.o

# Линкуем exe с ресурсами
g++ -std=c++17 -O2 -mwindows main.cpp build/resources.o \
    -o build/OBOR_Fonts.exe \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf libpng) \
    -lcomdlg32

cd build

echo "[2/3] Copying base DLLs..."
# Копируем заведомо нужные + все, что найдём
for dll in \
    SDL2.dll SDL2_image.dll SDL2_ttf.dll libpng16-16.dll \
    libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll zlib1.dll \
    libfreetype-6.dll libharfbuzz-0.dll libjpeg-8.dll libavif-16.dll \
    libwebp-7.dll libwebpdemux-2.dll libsharpyuv-0.dll libtiff-6.dll \
    libSvtAv1Enc-4.dll libSvtAv1Enc-3.dll librav1e.dll libaom.dll \
    libdav1d-7.dll libgav1.dll libyuv.dll libhwy.dll libjxl.dll \
    libjxl_cms.dll libhwycontrib.dll libbrotlidec.dll libbrotlienc.dll \
    libbrotlicommon.dll libglib-2.0-0.dll libgraphite2.dll \
    libpcre2-8-0.dll libintl-8.dll libiconv-2.dll libbz2-1.dll \
    libb2-1.dll libzstd.dll liblzma-5.dll libdeflate.dll libLerc.dll \
    libjbig-0.dll libz-1.dll ; do
    if [ -f "$MINGW/$dll" ] && [ ! -f "$dll" ]; then
        cp "$MINGW/$dll" .
    fi
done

echo "[3/3] Recursive copy of transitive DLLs (4 passes)..."
# Правильный парсинг ntldd: имя файла - последнее слово до " (0x"
for pass in 1 2 3 4; do
    CHANGED=0
    ntldd -R OBOR_Fonts.exe 2>/dev/null \
      | grep -iE '\.dll' \
      | sed -E 's/.*[\\\/]([^\\\/]+\.dll).*/\1/' \
      | grep -iE '\.dll$' \
      | sort -u \
      | while read dll; do
            case "$dll" in
                ext-ms-*|api-ms-*|PdmUtilities*|HvsiFileTrust*) continue ;;
            esac
            if [ -f "$MINGW/$dll" ] && [ ! -f "$dll" ]; then
                cp "$MINGW/$dll" .
                echo "  + $dll"
            fi
        done
    # если новых файлов не появилось - выходим
    NEW=$(ntldd -R OBOR_Fonts.exe 2>/dev/null \
      | grep -iE '\.dll' \
      | sed -E 's/.*[\\\/]([^\\\/]+\.dll).*/\1/' \
      | grep -iE '\.dll$' | sort -u \
      | while read dll; do
            case "$dll" in
                ext-ms-*|api-ms-*|PdmUtilities*|HvsiFileTrust*) continue ;;
            esac
            [ -f "$MINGW/$dll" ] && [ ! -f "$dll" ] && echo "$dll"
        done)
    [ -z "$NEW" ] && break
done

echo "=== Final check ==="
MISSING=$(ntldd -R OBOR_Fonts.exe 2>&1 \
    | grep "not found" \
    | grep -v "ext-ms-\|api-ms-\|PdmUtilities\|HvsiFileTrust" \
    | awk '{print $1}' | sort -u)
if [ -z "$MISSING" ]; then
    echo "OK: all dependencies resolved"
else
    echo "WARNING - still missing:"
    echo "$MISSING"
fi

echo "Done: $(pwd)/OBOR_Fonts.exe"
echo "DLLs copied: $(ls -1 *.dll 2>/dev/null | wc -l)"
