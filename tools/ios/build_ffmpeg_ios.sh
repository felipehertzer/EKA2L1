#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
FFMPEG_SRC="${FFMPEG_SRC:-$ROOT_DIR/src/external/ffmpeg}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-ios-ffmpeg}"
IOS_PLATFORM="${IOS_PLATFORM:-iphoneos}"
IOS_ARCH="${IOS_ARCH:-arm64}"
IOS_MIN_VERSION="${IOS_MIN_VERSION:-13.0}"
PREFIX="${PREFIX:-$BUILD_DIR/$IOS_PLATFORM-$IOS_ARCH}"

if [[ ! -x "$FFMPEG_SRC/configure" ]]; then
    echo "FFmpeg configure script not found at: $FFMPEG_SRC/configure" >&2
    exit 1
fi

SDK_PATH="$(xcrun --sdk "$IOS_PLATFORM" --show-sdk-path)"
CLANG="$(xcrun --sdk "$IOS_PLATFORM" --find clang)"

EXTRA_CFLAGS="-arch $IOS_ARCH -isysroot $SDK_PATH -miphoneos-version-min=$IOS_MIN_VERSION -fPIC -fno-common"
EXTRA_LDFLAGS="-arch $IOS_ARCH -isysroot $SDK_PATH -miphoneos-version-min=$IOS_MIN_VERSION"
ASM_OPTION=()

if ! command -v gas-preprocessor.pl >/dev/null 2>&1; then
    ASM_OPTION=(--disable-asm)
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$FFMPEG_SRC/configure" \
    --prefix="$PREFIX" \
    --target-os=darwin \
    --arch="$IOS_ARCH" \
    --cc="$CLANG" \
    --sysroot="$SDK_PATH" \
    --enable-cross-compile \
    --enable-pic \
    --enable-static \
    --disable-shared \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-avdevice \
    --disable-avfilter \
    --disable-postproc \
    --disable-network \
    --disable-audiotoolbox \
    "${ASM_OPTION[@]}" \
    --extra-cflags="$EXTRA_CFLAGS" \
    --extra-ldflags="$EXTRA_LDFLAGS"

make -j"$(sysctl -n hw.ncpu)"
make install

cat <<EOF
FFmpeg for iOS installed to:
  $PREFIX

Configure EKA2L1 with:
  -DEKA2L1_IOS_FFMPEG_ROOT=$PREFIX
EOF
