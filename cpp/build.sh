#!/bin/bash
# macOS build: Rust core (static lib) + Qt6 app bundle with PDFium, fonts and theme inside.
set -e
cd "$(dirname "$0")"
QT_DIR="${QT_DIR:-/opt/homebrew/opt/qt}"
(cd ../core-ffi && cargo build --release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" >/dev/null
cmake --build build -j8
APP="build/PDF DLP.app"; MACOS="$APP/Contents/MacOS"; RES="$APP/Contents/Resources"
mkdir -p "$RES/fonts"
cp pdfium/lib/libpdfium.dylib "$MACOS/" && install_name_tool -change ./libpdfium.dylib @executable_path/libpdfium.dylib "$MACOS/PDF DLP"
cp assets/fonts/* "$RES/fonts/" && cp src/theme.qss "$RES/"
codesign -s - -f --deep "$APP" >/dev/null 2>&1 || true
echo "built: $APP  (run: open \"$APP\" or \"$MACOS/PDF DLP\" file.pdf)"
