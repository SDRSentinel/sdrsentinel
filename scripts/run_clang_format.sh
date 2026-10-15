#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

TOTAL_LINES=0
FILE_COUNT=0

echo "Searching directories for C/C++ source files..."
CODE_FILES=$(find . -iregex '.*\.\(h\|hpp\|c\|cpp\)$')
while read -r CPP_FILE_PATH; do
    [ -z "$CPP_FILE_PATH" ] && continue

    # Skip external/third-party and build directories
    if [[ "$CPP_FILE_PATH" == "./.old"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./build"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./vcpkg_installed"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./core/libcorrect"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./core/std_replacement"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./core/src/imgui"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./misc_modules/discord_integration/discord-rpc"* ]]; then continue; fi
    if [[ "$CPP_FILE_PATH" == "./source_modules/sddc_source/src/libsddc"* ]]; then continue; fi
    
    if [ "$CPP_FILE_PATH" = "./core/src/json.hpp" ]; then continue; fi
    if [ "$CPP_FILE_PATH" = "./core/src/gui/file_dialogs.h" ]; then continue; fi

    echo "Formatting $CPP_FILE_PATH"
    clang-format --style=file -i "$CPP_FILE_PATH"

    TOTAL_LINES=$(( $TOTAL_LINES + $(wc -l < "$CPP_FILE_PATH") ))
    FILE_COUNT=$(( $FILE_COUNT + 1 ))
done <<< "$CODE_FILES"

echo "Formatting completed!"
echo "Total Lines: $TOTAL_LINES"
echo "Total Files Formatted: $FILE_COUNT"