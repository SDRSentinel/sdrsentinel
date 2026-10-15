#!/bin/sh
set -eu

fail() {
    echo "Error: $*" >&2
    exit 1
}

[ "$#" -le 2 ] || fail "Usage: $0 [build-directory] [runtime-dependencies]"

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
VERSION_HEADER="$SCRIPT_DIR/../../core/src/version.h"
BUILD_DIR="${1:-./build}"
DEPENDS="${2:-libfftw3-bin, libglfw3, libportaudio2, libzstd1, libusb-1.0-0}"
PKG_NAME=sdrsentinel
OUTPUT_DIR=$PWD

command -v cmake >/dev/null 2>&1 || fail "cmake is required"
command -v dpkg-deb >/dev/null 2>&1 || fail "dpkg-deb is required"
[ -f "$VERSION_HEADER" ] || fail "Version header not found: $VERSION_HEADER"
[ -f "$BUILD_DIR/CMakeCache.txt" ] || fail "CMake build directory not found: $BUILD_DIR"
[ -f "$BUILD_DIR/cmake_install.cmake" ] || fail "CMake install script not found: $BUILD_DIR"
BUILD_DIR=$(CDPATH='' cd -- "$BUILD_DIR" && pwd)

VERSION=$(sed -n 's/^#define VERSION_STR "\([^"]*\)"[[:space:]]*$/\1/p' "$VERSION_HEADER")
[ -n "$VERSION" ] || fail "VERSION_STR not found in $VERSION_HEADER"
VERSION="${VERSION}${BUILD_NO:-}"
case "$VERSION" in
    [0-9]*) ;;
    *) fail "Debian package version must start with a digit: $VERSION" ;;
esac
case "$VERSION" in
    *[!A-Za-z0-9.+:~-]*) fail "Invalid Debian package version: $VERSION" ;;
esac

if [ -n "${DEB_ARCH:-}" ]; then
    ARCH=$DEB_ARCH
else
    command -v dpkg >/dev/null 2>&1 || fail "dpkg or a DEB_ARCH override is required"
    ARCH=$(dpkg --print-architecture)
fi
case "$ARCH" in
    ''|*[!a-z0-9-]*) fail "Invalid Debian architecture: $ARCH" ;;
esac

# Control fields must occupy a single line.
NEWLINE='
'
CARRIAGE_RETURN=$(printf '\r')
case "$DEPENDS" in
    *"$NEWLINE"*|*"$CARRIAGE_RETURN"*) fail "Runtime dependencies must be a single line" ;;
esac

WORK_DIR=$(mktemp -d "$OUTPUT_DIR/.${PKG_NAME}_package.XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT
trap 'exit 1' HUP INT TERM
STAGING_DIR="$WORK_DIR/root"
mkdir -p "$STAGING_DIR"

echo "Staging installation from $BUILD_DIR"
DESTDIR="$STAGING_DIR" cmake --install "$BUILD_DIR" --config Release

# A successful installer with only resources (or no files) is not a usable package.
INSTALLED_BINARY=$(find "$STAGING_DIR" -type f -path '*/bin/sdrpp' -print)
[ -n "$INSTALLED_BINARY" ] && [ -s "$INSTALLED_BINARY" ] && [ -x "$INSTALLED_BINARY" ] ||
    fail "Installation did not produce a nonempty executable bin/sdrpp"

mkdir -p "$STAGING_DIR/DEBIAN"
cat > "$STAGING_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Maintainer: SDRSentinel Project <zaryob.dev@gmail.com>
Architecture: $ARCH
Description: Passive RF signal characterization, pulse analysis, and direction finding suite based on SDR++
Provides: sdrpp
Depends: $DEPENDS
EOF

echo "Creating Debian package for $ARCH"
dpkg-deb --root-owner-group --build "$STAGING_DIR" "$WORK_DIR/package.deb"
[ -s "$WORK_DIR/package.deb" ] || fail "dpkg-deb did not produce a package"
OUTPUT="$OUTPUT_DIR/${PKG_NAME}_debian_${ARCH}.deb"
mv "$WORK_DIR/package.deb" "$OUTPUT"

# Legacy Docker workflows still collect this fixed filename on every architecture.
cp "$OUTPUT" "$OUTPUT_DIR/sdrpp_debian_amd64.deb"
echo "Created $OUTPUT"
