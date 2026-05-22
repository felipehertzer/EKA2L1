## Required programs

For the current cross-platform support status, feature parity gaps, and tested build commands, see [docs/PLATFORM_MATRIX.md](docs/PLATFORM_MATRIX.md).

### For all targets

- Git: Required to clone the source code of EKA2L1
- CMake 3.21 or newer: Used to generate the local build tree.
- Qt 6.11.1 or newer compatible Qt 6 release: This is used to build the UI of the emulator on PC platforms.
- Python: The emulator calls some additional Python scripts for extra special build process.

### Specific target

- Android:
    * Android Studio: Develop and compile to Android platfrom through this IDE.
    * NDK: The Android SDK.

- Windows:
    * Visual Studio 2022 or newer, coming with support for C++20 code.

- macOS:
    * Xcode 15 or newer.

- Linux:
    * clang/g++: Clang 14 or GCC 13 or newer is recommended.

### Optional

- Symbian SDKs (only on Windows!): EKA2L1 contains code that can compile to native Symbian DLL, with the intention of replacing the original DLL files shipped with phones with another DLL versions that interact directly with the emulator. SDKs that should be installed are: **S60v5, S60v2fp2 and S^3** SDKs. **Make sure** the EKA2L1 source code and the Symbian SDKs are *on the same drive*.

## Retrieve the source code

- Use a preferred Git client, or the command line tool with the command ```git clone --recurse-submodules https://github.com/EKA2L1/EKA2L1``` to clone the [EKA2L1](https://github.com/EKA2L1/EKA2L1) Github source code.

- EKA2L1 makes use of many code repositories as dependencies, so initializing and cloning all submodules are required. If the Git client has not done the job yet (you can go check the folder ```src/external``` and see if content of child folders are empty or not), or you forgot to clone the repository with the ```--recurse-submodules``` flag, use this command to update and clone the submodules: ```git submodule update --init --recursive```.

## Build the emulator

- It's recommended to build the emulator with **RelWithDebInfo** configuration for debugging purposes.

### On Windows, MacOS and Linux distros

- During the installation of the Qt framework, if you use the official install tool, you might already have Qt Creator installed. The choice of using another IDE or using Qt Creator is up to you:

    * If you are using Qt Creator: there should be no additional setup needed, open the CMakeLists.txt in the root folder of the EKA2L1 source code, which will then automatically setup the project in the IDE for you. Choose the preferred build configuration and build the eka2l1_qt target to generate the UI executable.

    * If you are not using Qt Creator: keep local builds in the repository-root `build` directory. Configure and build with:

      ```sh
      cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/Qt
      cmake --build --preset default
      ```

      Additional local presets are available for common build modes:

      ```sh
      cmake --preset debug
      cmake --preset release
      cmake --preset dev-sanitize
      ```

      If you prefer not to use presets, use the same root build directory explicitly:

      ```sh
      cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=/path/to/Qt
      cmake --build build --target eka2l1_qt
      ```

      For example, on Windows with Visual Studio you can set `CMAKE_PREFIX_PATH` to `D:\Programs\Qt\6.11.1\msvc2022_64`.

### Source formatting and linting

Local developer targets are available after configuring CMake. They operate on EKA2L1 source code and skip vendored dependency code under `src/external` plus every submodule path recorded in `.gitmodules`.

```sh
cmake --build build --target format
cmake --build build --target format-check
cmake --build build --target lint
```

- `format` applies `.clang-format` to tracked and untracked non-ignored C/C++/Objective-C++ project source files.
- `format-check` verifies formatting without modifying files.
- `lint` runs `clang-tidy` using `build/compile_commands.json` and `.clang-tidy`.
- `cmake --build --preset format`, `cmake --build --preset format-check`, and `cmake --build --preset lint` are equivalent preset entry points.

Optional developer checks are available at configure time:

```sh
cmake --preset dev-sanitize
cmake --preset default -DEKA2L1_ENABLE_STRICT_WARNINGS=ON
```

### On Android

- With Android Studio opened, navigate to File/Open and choose the ```source code root/src/android/``` folder. The android project for EKA2L1 should setup and ready.
