# Platform Matrix

This is the current portability target for keeping the desktop, Android, and iOS ports close to feature parity while still letting constrained platforms build.

Desktop Qt builds target Qt 6.11.1. The official Qt 6.11 binaries require a modern desktop baseline: macOS 13 or newer, Windows with MSVC 2022, and Linux with a recent glibc.

## Status

| Platform | Target | Status | Feature notes |
| --- | --- | --- | --- |
| Windows x86_64 | `eka2l1_qt` | Supported by CMake. Not verified on this host. | Uses vendored FFmpeg for MSVC/MinGW x86_64. Dynarmic, Qt UI, Vulkan, scripting, and UPnP are intended to be available. |
| Linux x86_64/aarch64 | `eka2l1_qt` | Supported by CMake. Not verified on this host. | Uses vendored FFmpeg when the matching arch package exists, otherwise `pkg-config`. Dynarmic is enabled on supported CPUs. |
| macOS arm64/x86_64 | `eka2l1_qt` | Verified locally on macOS arm64. | Uses vendored FFmpeg when the arch package exists, otherwise Homebrew/pkg-config FFmpeg. Bundle fixup searches Homebrew dependency paths. Vulkan presents through MoltenVK/Metal. |
| Android arm64-v8a/armeabi-v7a/x86/x86_64 | Android app | Supported by the Android project. Not verified on this host. | Uses Android ABI FFmpeg packages. Android has native input, storage, camera, vibration, audio, and launcher integration. Building the Gradle project requires a current JDK, Android SDK, and NDK. |
| iOS arm64/simulator | `eka2l1_iOS` | Simulator was verified locally. | Scripting is disabled by default. FFmpeg is enabled only when `EKA2L1_IOS_FFMPEG_ROOT` or `EKA2L1_FFMPEG_ROOT` points to an iOS FFmpeg install. Dynarmic/JIT is disabled unless `EKA2L1_IOS_USE_DYNARMIC_JIT=ON` is used for an entitled sideload/JIT build. |
| PS Vita | `EKA2L1_Vita` | Supported by the VitaSDK CMake path. Native backend and networking cross-build verified locally. | Vita has no Vulkan WSI, so it uses `graphic_api::native`: the shared graphics-driver command layer presents through SDL2's Vita/GXM path. Networking is enabled through SceNet/SceNetCtl and a reduced libuv/uvw build. Full GXM acceleration still needs a backend pass. |

## Shared Build Options

| Option | Default | Purpose |
| --- | --- | --- |
| `EKA2L1_ENABLE_FFMPEG` | `ON`, except iOS without an FFmpeg root | FFmpeg-backed audio/video decoding. |
| `EKA2L1_FFMPEG_ROOT` | empty | Explicit FFmpeg install root with `include/` and `lib/`. Works for all platforms. |
| `EKA2L1_IOS_FFMPEG_ROOT` | empty | iOS-specific FFmpeg root. |
| `EKA2L1_ENABLE_DYNARMIC` | `ON`, except iOS without JIT entitlement | CPU recompiler. Disabled automatically for ARM32. |
| `EKA2L1_IOS_USE_DYNARMIC_JIT` | `OFF` | Enables the iOS JIT path only for builds that can legally map executable memory. |
| `EKA2L1_ENABLE_SCRIPTING_ABILITY` | `ON`, forced `OFF` on iOS | Lua/Python scripting support. |
| `EKA2L1_ENABLE_UPNP` | `ON`, forced `OFF` on Vita | UPnP port mapping. Disable when platform networking dependencies are unavailable. |
| `EKA2L1_ENABLE_NETWORKING` | `ON` | libuv-backed socket and Bluetooth networking protocols. Socket services still build with no registered inet protocols when disabled. Vita uses SceNet/SceNetCtl with unsupported libuv features returning `UV_ENOSYS`. |
| `EKA2L1_BUILD_VULKAN_BACKEND` | `ON`, forced `OFF` on Vita | Vulkan renderer used by desktop and mobile platforms with Vulkan or MoltenVK support. Configure fails if enabled and the Vulkan SDK/backend dependencies are unavailable. |
| `EKA2L1_BUILD_NATIVE_BACKEND` | `OFF`, forced `ON` on Vita | Non-OpenGL native backend slot for platforms without Vulkan WSI. Vita currently uses this through SDL2/GXM presentation. |

## Reference Builds

### macOS Qt

```sh
cmake -S . -B build-macos-portability \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
  -DEKA2L1_BUILD_TESTS=OFF \
  -DEKA2L1_BUILD_TOOLS=OFF \
  -DEKA2L1_ENABLE_SCRIPTING_ABILITY=OFF \
  -DEKA2L1_ENABLE_UPNP=OFF \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-macos-portability --target eka2l1_qt --parallel
```

### iOS Simulator

```sh
cmake -S . -B build-ios-sim \
  -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO

cmake --build build-ios-sim --target eka2l1_iOS --config Debug
```

To include FFmpeg on iOS, build/install an iOS FFmpeg package first and add:

```sh
-DEKA2L1_IOS_FFMPEG_ROOT=/path/to/ios-ffmpeg
```

### Android

Open `src/emu/android` in Android Studio, or build with Gradle after installing a current JDK, Android SDK, and NDK:

```sh
cd src/emu/android
./gradlew assembleDebug
```

### PS Vita

Install VitaSDK and an SDL2 build for Vita first. Then configure with the VitaSDK toolchain:

```sh
cmake -S . -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-vita --target EKA2L1_Vita --parallel
```

The build creates a `EKA2L1_Vita` executable and, when VitaSDK's package helpers are available, `.self`/`.vpk` artifacts. Copy the generated `src/emu/vita/eka2l1_vita_resources` contents and an installed EKA2L1 `data` folder to `ux0:data/eka2l1/` on the Vita.

Networking is enabled by default on Vita. The app initializes `SceNet` and `SceNetCtl` at startup, links the Vita networking stubs, and builds libuv/uvw in a network-focused configuration. UPnP is still disabled on Vita because miniupnp has not been ported to the VitaSDK networking stack.

### Linux Qt

```sh
cmake -S . -B build-linux \
  -DCMAKE_PREFIX_PATH=/path/to/qt \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build-linux --target eka2l1_qt --parallel
```

Install FFmpeg development packages if no matching vendored FFmpeg package exists for the host architecture.

### Windows Qt

```bat
cmake -S . -B build-windows -G "Visual Studio 17 2022" ^
  -A x64 ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64

cmake --build build-windows --target eka2l1_qt --config RelWithDebInfo
```

## Feature Parity Gaps

- PS Vita now has a non-OpenGL native backend path, but its current implementation is a CPU/SDL2 presentation layer. A real GXM command translator is still needed for full 3D feature parity.
- PS Vita networking supports the normal socket/resolver path through SceNet-backed libuv. Source-specific multicast, Unix-domain sockets, process/signal/file-watch libuv features, and IPv6 interface scope-name lookup remain unsupported and return `UV_ENOSYS` where applicable.
- iOS cannot be fully identical to Android unless the build has JIT entitlement support and an iOS FFmpeg package. Without that, CPU recompilation and FFmpeg media decoding are intentionally disabled.
- Android has the most complete mobile platform integration today: launcher, storage, camera, vibration, touch/input, and native app lifecycle.
- Desktop ports share the Qt frontend and now use the Vulkan backend for presentation.
- Scripting is not a universal feature because LuaJIT and Python embedding are awkward on iOS and likely unsuitable for Vita without a separate porting pass.
