#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PACKAGER="$SCRIPT_DIR/../make_debian_package.sh"
EXPECTED_VERSION=$(awk '$2 == "VERSION_STR" { gsub(/"/, "", $3); print $3 }' "$SCRIPT_DIR/../../../core/src/version.h")
REAL_DPKG_DEB=$(command -v dpkg-deb || true)
export REAL_DPKG_DEB
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/sdrsentinel-packaging.XXXXXX")
trap 'rm -rf "$TEST_DIR"' EXIT
trap 'exit 1' HUP INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

read_control() {
    if [ -n "$REAL_DPKG_DEB" ]; then
        "$REAL_DPKG_DEB" --field "$1"
    else
        cat "$1"
    fi
}

mkdir -p "$TEST_DIR/tools" "$TEST_DIR/source" "$TEST_DIR/default" "$TEST_DIR/override"
cat > "$TEST_DIR/tools/dpkg" <<'EOF'
#!/bin/sh
[ "$#" -eq 1 ] && [ "$1" = --print-architecture ] || exit 1
echo arm64
EOF
cat > "$TEST_DIR/tools/dpkg-deb" <<'EOF'
#!/bin/sh
set -eu
[ "$#" -eq 4 ] && [ "$1" = --root-owner-group ] && [ "$2" = --build ] || exit 1
[ "${MOCK_DPKG_DEB_FAIL:-0}" = 0 ] || exit 1
if [ -n "${REAL_DPKG_DEB:-}" ]; then
    exec "$REAL_DPKG_DEB" "$@"
fi
# Save the actual control file as the mock package so assertions inspect its fields.
cp "$3/DEBIAN/control" "$4"
EOF
chmod +x "$TEST_DIR/tools/dpkg" "$TEST_DIR/tools/dpkg-deb"
PATH="$TEST_DIR/tools:$PATH"
export PATH

printf '#!/bin/sh\nexit 0\n' > "$TEST_DIR/source/sdrpp"
: > "$TEST_DIR/source/empty_sdrpp"
cat > "$TEST_DIR/source/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.13)
project(PackagingFixture NONE)
if(FIXTURE_MODE STREQUAL "failure")
    install(CODE "message(FATAL_ERROR \"Expected installer failure\")")
elseif(FIXTURE_MODE STREQUAL "empty-binary")
    install(PROGRAMS empty_sdrpp DESTINATION bin RENAME sdrpp)
elseif(NOT FIXTURE_MODE STREQUAL "empty")
    install(PROGRAMS sdrpp DESTINATION bin)
endif()
EOF

# Exercise CMake's real install staging, including paths containing spaces.
BUILD_DIR="$TEST_DIR/build with spaces"
cmake -S "$TEST_DIR/source" -B "$BUILD_DIR" -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
(
    cd "$TEST_DIR/default"
    BUILD_NO=-42 sh "$PACKAGER" "$BUILD_DIR" >/dev/null
    BUILD_NO=-42 sh "$PACKAGER" "$BUILD_DIR" >/dev/null
)
PACKAGE="$TEST_DIR/default/sdrsentinel_debian_arm64.deb"
[ -s "$PACKAGE" ] || fail "Native architecture package was not produced"
read_control "$PACKAGE" > "$TEST_DIR/default/control"
grep -Fxq "Version: $EXPECTED_VERSION-42" "$TEST_DIR/default/control" || fail "Version was not derived from version.h and BUILD_NO"
grep -qx 'Architecture: arm64' "$TEST_DIR/default/control" || fail "Native architecture was not detected"
grep -qx 'Depends: libfftw3-bin, libglfw3, libportaudio2, libzstd1, libusb-1.0-0' "$TEST_DIR/default/control" || fail "Default dependencies were lost"
[ "$(grep -c '^Package:' "$TEST_DIR/default/control")" -eq 1 ] || fail "Repeated packaging duplicated control fields"
cmp "$PACKAGE" "$TEST_DIR/default/sdrpp_debian_amd64.deb" || fail "Legacy alias differs"

(
    cd "$TEST_DIR/override"
    DEB_ARCH=amd64 BUILD_NO=-43 sh "$PACKAGER" "$BUILD_DIR" 'libfixture1 (>= 1.2)' >/dev/null
)
PACKAGE="$TEST_DIR/override/sdrsentinel_debian_amd64.deb"
read_control "$PACKAGE" > "$TEST_DIR/override/control"
grep -qx 'Architecture: amd64' "$TEST_DIR/override/control" || fail "Architecture override was ignored"
grep -qx 'Depends: libfixture1 (>= 1.2)' "$TEST_DIR/override/control" || fail "Explicit dependencies were ignored"

for MODE in failure empty empty-binary; do
    cmake -S "$TEST_DIR/source" -B "$TEST_DIR/build-$MODE" -DFIXTURE_MODE="$MODE" >/dev/null
    mkdir -p "$TEST_DIR/$MODE"
    if (cd "$TEST_DIR/$MODE" && sh "$PACKAGER" "$TEST_DIR/build-$MODE" >log 2>&1); then
        fail "Packaging unexpectedly succeeded for $MODE installation"
    fi
done

mkdir -p "$TEST_DIR/missing" "$TEST_DIR/packager-failure" "$TEST_DIR/invalid-arch" "$TEST_DIR/invalid-depends"
if (cd "$TEST_DIR/missing" && sh "$PACKAGER" "$TEST_DIR/does-not-exist" >log 2>&1); then
    fail "Packaging accepted a missing build directory"
fi
if (cd "$TEST_DIR/packager-failure" && MOCK_DPKG_DEB_FAIL=1 sh "$PACKAGER" "$BUILD_DIR" >log 2>&1); then
    fail "Packaging ignored dpkg-deb failure"
fi
if (cd "$TEST_DIR/invalid-arch" && DEB_ARCH='../invalid' sh "$PACKAGER" "$BUILD_DIR" >log 2>&1); then
    fail "Packaging accepted an invalid architecture"
fi
BAD_DEPENDS=$(printf 'libfixture1\nInjected: field')
if (cd "$TEST_DIR/invalid-depends" && sh "$PACKAGER" "$BUILD_DIR" "$BAD_DEPENDS" >log 2>&1); then
    fail "Packaging accepted multiline dependencies"
fi

for CASE in default override failure empty empty-binary missing packager-failure invalid-arch invalid-depends; do
    [ -z "$(find "$TEST_DIR/$CASE" -name '.sdrsentinel_package.*' -print)" ] || fail "Temporary staging was not cleaned up for $CASE"
done
for CASE in failure empty empty-binary missing packager-failure invalid-arch invalid-depends; do
    [ -z "$(find "$TEST_DIR/$CASE" -name '*.deb' -print)" ] || fail "Failed $CASE run produced a package"
done

if [ -n "$REAL_DPKG_DEB" ]; then
    echo "Debian packaging tests passed (real CMake staging and dpkg-deb)."
else
    echo "Debian packaging tests passed (real CMake staging; mocked dpkg tools)."
fi
