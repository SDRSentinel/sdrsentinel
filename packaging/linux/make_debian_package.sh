#!/bin/sh

# Create directory structure
echo "Create directory structure"
PKG_NAME=sdrsentinel
mkdir -p ${PKG_NAME}_debian_amd64/DEBIAN

# Create package info
echo "Create package info"
echo "Package: ${PKG_NAME}" >> ${PKG_NAME}_debian_amd64/DEBIAN/control
echo "Version: 1.0.0${BUILD_NO}" >> ${PKG_NAME}_debian_amd64/DEBIAN/control
echo "Maintainer: SDRSentinel Project <zaryob.dev@gmail.com>" >> ${PKG_NAME}_debian_amd64/DEBIAN/control
echo "Architecture: amd64" >> ${PKG_NAME}_debian_amd64/DEBIAN/control
echo "Description: Passive RF signal characterization, pulse analysis, and direction finding suite based on SDR++" >> ${PKG_NAME}_debian_amd64/DEBIAN/control
echo "Provides: sdrpp" >> ${PKG_NAME}_debian_amd64/DEBIAN/control
echo "Depends: $2" >> ${PKG_NAME}_debian_amd64/DEBIAN/control

# Copying files
ORIG_DIR=$PWD
BUILD_DIR="${1:-./build}"
DEPENDS="${2:-libfftw3-bin, libglfw3, libportaudio2, libzstd1, libusb-1.0-0}"

cd "$BUILD_DIR"
if [ -f "build.ninja" ]; then
    ninja install DESTDIR="$ORIG_DIR/${PKG_NAME}_debian_amd64"
else
    make install DESTDIR="$ORIG_DIR/${PKG_NAME}_debian_amd64"
fi
cd "$ORIG_DIR"

# Create package
echo "Create package"
dpkg-deb --build ${PKG_NAME}_debian_amd64
cp ${PKG_NAME}_debian_amd64.deb sdrpp_debian_amd64.deb 2>/dev/null || true

# Cleanup
echo "Cleanup"
rm -rf ${PKG_NAME}_debian_amd64
