#!/bin/sh
set -eu

# ========================= Boilerplate =========================
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <build-directory> <output.app>" >&2
    exit 1
fi

BUILD_DIR=$1
BUNDLE=$2

case "$BUNDLE" in
    *.app) ;;
    *) echo "Bundle output must end in .app: $BUNDLE" >&2; exit 1 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

. "$SCRIPT_DIR/bundle_utils.sh"

VERSION=$(sed -n 's/^#define VERSION_STR "\([^"]*\)".*/\1/p' "$PROJECT_ROOT/core/src/version.h")
if [ -z "$VERSION" ]; then
    echo "Could not read VERSION_STR from core/src/version.h" >&2
    exit 1
fi

# Validate required build outputs before replacing an existing bundle.
for BINARY in "$BUILD_DIR/sdrpp" "$BUILD_DIR/core/libsdrpp_core.dylib"; do
    if [ ! -f "$BINARY" ]; then
        echo "Missing required build output: $BINARY" >&2
        exit 1
    fi
done

for MODULE in rf_tools kraken_doa; do
    OPTION=$(printf '%s' "$MODULE" | tr '[:lower:]' '[:upper:]')
    if [ -f "$BUILD_DIR/CMakeCache.txt" ] &&
        grep -q "^OPT_BUILD_${OPTION}:BOOL=ON$" "$BUILD_DIR/CMakeCache.txt" &&
        [ ! -f "$BUILD_DIR/misc_modules/$MODULE/$MODULE.dylib" ]; then
        echo "Missing enabled module: $MODULE" >&2
        exit 1
    fi
done

# ========================= Prepare dotapp structure =========================

# Clear .app
rm -rf "$BUNDLE"

# Create .app structure
bundle_create_struct "$BUNDLE"

# Add resources
cp -R "$PROJECT_ROOT/root/res/"* "$BUNDLE/Contents/Resources/"

# Create the icon file
bundle_create_icns "$PROJECT_ROOT/root/res/icons/sdrsentinel.macos.png" "$BUNDLE/Contents/Resources/sdrsentinel"

# Create the property list
bundle_create_plist sdrsentinel SDRSentinel org.sdrsentinel.app "$VERSION" sdrs sdrpp sdrsentinel "$BUNDLE/Contents/Info.plist"

# ========================= Install binaries =========================

# Core
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/MacOS" "$BUILD_DIR/sdrpp"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Frameworks" "$BUILD_DIR/core/libsdrpp_core.dylib"

# Source modules
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/airspy_source/airspy_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/airspyhf_source/airspyhf_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/bladerf_source/bladerf_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/dragonlabs_source/dragonlabs_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/file_source/file_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/fobossdr_source/fobossdr_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/hackrf_source/hackrf_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/hermes_source/hermes_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/hydrasdr_source/hydrasdr_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/limesdr_source/limesdr_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/network_source/network_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/perseus_source/perseus_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/plutosdr_source/plutosdr_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/rfnm_source/rfnm_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/rfspace_source/rfspace_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/rtl_sdr_source/rtl_sdr_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/rtl_tcp_source/rtl_tcp_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/sdrplay_source/sdrplay_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/sdrpp_server_source/sdrpp_server_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/spyserver_source/spyserver_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/badgesdr_source/badgesdr_source.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/spectran_http_source/spectran_http_source.dylib"
# bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/source_modules/usrp_source/usrp_source.dylib"

# Sink modules
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/sink_modules/portaudio_sink/audio_sink.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/sink_modules/network_sink/network_sink.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/sink_modules/new_portaudio_sink/new_portaudio_sink.dylib"

# Decoder modules
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/decoder_modules/atv_decoder/atv_decoder.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/decoder_modules/m17_decoder/m17_decoder.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/decoder_modules/meteor_demodulator/meteor_demodulator.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/decoder_modules/pager_decoder/pager_decoder.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/decoder_modules/radio/radio.dylib"

# Misc modules
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/discord_integration/discord_integration.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/frequency_manager/frequency_manager.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/iq_exporter/iq_exporter.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/recorder/recorder.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/rf_tools/rf_tools.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/kraken_doa/kraken_doa.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/rigctl_client/rigctl_client.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/rigctl_server/rigctl_server.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/scanner/scanner.dylib"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/misc_modules/scheduler/scheduler.dylib"

# ========================= Finalize =========================

# Sign the app
bundle_sign "$BUNDLE"
