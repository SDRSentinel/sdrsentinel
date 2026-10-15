#!/bin/sh
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

mkdir -p "$PROJECT_ROOT/root_dev"
cp -r "$PROJECT_ROOT/root/"* "$PROJECT_ROOT/root_dev/"
echo "Initialized root_dev from root."
