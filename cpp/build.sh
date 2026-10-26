#!/bin/bash
# Build the native prototype on macOS. Needs: brew install qt ; pdfium prebuilt (bblanchon/pdfium-binaries) extracted to $PDFIUM_DIR
set -e
cd "$(dirname "$0")"
PDFIUM_DIR="${PDFIUM_DIR:-$HOME/Projects/pdf-dlp/cpp/pdfium}"
QT_DIR="${QT_DIR:-/opt/homebrew/opt/qt}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" -DPDFIUM_DIR="$PDFIUM_DIR"
cmake --build build -j8
echo "run: ./build/pdfdlp_native.app/Contents/MacOS/pdfdlp_native <file.pdf>"
