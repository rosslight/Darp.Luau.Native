#!/bin/sh
set -eu

# Usage:
#   ./scripts/build-native.sh <OutputDir> [Configuration]
# Examples:
#   ./scripts/build-native.sh ../artifacts/native
#   ./scripts/build-native.sh ../artifacts/native Debug

OUTDIR="${1:?Usage: $0 <OutputDir> [Configuration]}"
CONFIG="${2:-Release}"

# Script directory (absolute)
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

# Match your PS1:
# BuildDir = GetFullPath(ScriptDir/../artifacts/build)
# SourceDir = GetFullPath(ScriptDir/../native)
BUILD_DIR="$(cd "$SCRIPT_DIR/../artifacts" && pwd)/build"
SOURCE_DIR="$(cd "$SCRIPT_DIR/../native" && pwd)"

echo "Build directory:  $BUILD_DIR"
echo "Source directory: $SOURCE_DIR"
echo "Output directory: $OUTDIR"
echo ""

# Ensure output directory exists (relative to caller, same as your PS1 behavior)
mkdir -p "$OUTDIR"

# Clean build directory (then recreate)
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Configure + build
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --parallel

# Find the built shared library (your target is "luau")
LIB=""
if [ -f "$BUILD_DIR/libluau.so" ]; then
  LIB="$BUILD_DIR/libluau.so"
elif [ -f "$BUILD_DIR/libluau.dylib" ]; then
  LIB="$BUILD_DIR/libluau.dylib"
else
  echo "ERROR: Built library not found in $BUILD_DIR"
  echo "Looked for: libluau.so, libluau.dylib"
  exit 1
fi

cp -f "$LIB" "$OUTDIR/"
echo "Copied $LIB -> $OUTDIR"
