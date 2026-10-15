#!/bin/bash
set -e
cd /root

echo "==> Updating package repositories..."
apt-get update

# Core build dependencies
DEPS=(
    build-essential cmake git libfftw3-dev libglfw3-dev libzstd-dev
    libairspyhf-dev libairspy-dev libiio-dev libad9361-dev librtaudio-dev
    libhackrf-dev librtlsdr-dev libbladerf-dev liblimesuite-dev p7zip-full
    wget portaudio19-dev libcodec2-dev autoconf libtool xxd libspdlog-dev
    pkg-config libeigen3-dev
)

# Detect appropriate Volk package name
if apt-cache show libvolk-dev >/dev/null 2>&1; then
    DEPS+=(libvolk-dev)
    VOLK_PKG="libvolk-dev"
elif apt-cache show libvolk2-dev >/dev/null 2>&1; then
    DEPS+=(libvolk2-dev)
    VOLK_PKG="libvolk2-dev"
elif apt-cache show libvolk1-dev >/dev/null 2>&1; then
    DEPS+=(libvolk1-dev)
    VOLK_PKG="libvolk1-dev"
else
    VOLK_PKG="libvolk-dev"
fi

echo "==> Installing dependencies (${DEPS[*]})..."
apt-get install -y "${DEPS[@]}"

# Install SDRPlay API libraries
SDRPLAY_ARCH=$(dpkg --print-architecture)
if wget -q https://www.sdrpp.org/SDRplay_RSP_API-Linux-3.15.2.run || wget -q https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run; then
    7z x ./SDRplay_RSP_API-Linux-3.15.2.run -y >/dev/null 2>&1 || true
    7z x ./SDRplay_RSP_API-Linux-3.15.2 -y >/dev/null 2>&1 || true
    if [ -f "$SDRPLAY_ARCH/libsdrplay_api.so.3.15" ]; then
        cp "$SDRPLAY_ARCH/libsdrplay_api.so.3.15" /usr/lib/libsdrplay_api.so
        cp inc/* /usr/include/ 2>/dev/null || true
    fi
fi

# Install libperseus
if [ ! -d "libperseus-sdr" ]; then
    git clone https://github.com/Microtelecom/libperseus-sdr
    cd libperseus-sdr
    autoreconf -i
    ./configure
    make -j$(nproc)
    make install
    cd ..
fi

# Install librfnm
if [ ! -d "librfnm" ]; then
    git clone https://github.com/AlexandreRouma/librfnm
    cd librfnm
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    make install
    cd ../..
fi

# Install libfobos
if [ ! -d "libfobos" ]; then
    git clone https://github.com/AlexandreRouma/libfobos
    cd libfobos
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    make install
    cd ../..
fi

# Install libhydrasdr
if [ ! -d "rfone_host" ]; then
    git clone https://github.com/hydrasdr/rfone_host
    cd rfone_host
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    make install
    cd ../..
fi

# Install libdlcr
if [ ! -d "dlcr_host" ]; then
    if wget -q https://dragnlabs.com/host-tools/dlcr_host_v0.3.0.zip; then
        mkdir -p dlcr_host && cd dlcr_host
        7z x ../dlcr_host_v0.3.0.zip -y >/dev/null 2>&1 || true
        mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)
        make install
        cd ../..
    fi
fi

# Locate SDRSentinel / SDRPlusPlus repo directory
if [ -d "/root/SDRSentinel" ]; then
    REPO_DIR="/root/SDRSentinel"
elif [ -d "/root/SDRPlusPlus" ]; then
    REPO_DIR="/root/SDRPlusPlus"
else
    echo "ERROR: SDRSentinel repository mount not found in /root/SDRSentinel or /root/SDRPlusPlus!"
    exit 1
fi

cd "$REPO_DIR"
rm -rf build
mkdir -p build
cd build

echo "==> Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPT_BUILD_BLADERF_SOURCE=ON \
    -DOPT_BUILD_LIMESDR_SOURCE=ON \
    -DOPT_BUILD_SDRPLAY_SOURCE=ON \
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON \
    -DOPT_BUILD_M17_DECODER=ON \
    -DOPT_BUILD_PERSEUS_SOURCE=ON \
    -DOPT_BUILD_RFNM_SOURCE=ON \
    -DOPT_BUILD_FOBOSSDR_SOURCE=ON \
    -DOPT_BUILD_HYDRASDR_SOURCE=ON \
    -DOPT_BUILD_DRAGONLABS_SOURCE=ON \
    -DOPT_BUILD_RF_TOOLS=ON \
    -DOPT_BUILD_KRAKEN_DOA=ON

echo "==> Building binaries..."
make VERBOSE=1 -j$(nproc)

cd "$REPO_DIR"
echo "==> Generating Debian package..."
sh packaging/linux/make_debian_package.sh ./build "libfftw3-dev, libglfw3-dev, ${VOLK_PKG}, librtaudio-dev, libzstd-dev"

echo "==> Build and packaging complete!"
