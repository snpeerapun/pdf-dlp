# bundle pdfium next to the binary (prebuilt dylib has install name ./libpdfium.dylib)
B=build/pdfdlp_native.app/Contents/MacOS
cp "$PDFIUM_DIR/lib/libpdfium.dylib" "$B/" && install_name_tool -change ./libpdfium.dylib @executable_path/libpdfium.dylib "$B/pdfdlp_native" && codesign -s - -f "$B/pdfdlp_native" "$B/libpdfium.dylib"
