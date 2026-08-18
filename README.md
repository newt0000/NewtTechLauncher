# NewtTech Launcher — CLion Bundled Toolchain Edition

This version intentionally removes Qt and all other external development dependencies.

It is a pure Windows C++20 project using:

- Win32 API for the desktop UI
- WinHTTP for HTTPS/HTTP
- a small bundled JSON parser
- CMake
- standard Windows system libraries

This means CLion can build it using:

- Bundled MinGW
- Bundled CMake
- Bundled Ninja
- Bundled GDB

There is no Qt install requirement and no `CMAKE_PREFIX_PATH`.

## Open in CLion

Open this folder as the project:

```text
NewtTechLauncher_CLion_Bundled
```

CLion should detect the top-level:

```text
CMakeLists.txt
```

If needed, right-click `CMakeLists.txt` and select **Load CMake Project**.

## CLion toolchain

Use the default/bundled MinGW toolchain.

You do not need to point it at `C:\Qt`.

A normal toolchain configuration should look approximately like:

```text
Toolset:      Bundled MinGW
CMake:        Bundled
Build Tool:   Ninja
C Compiler:   Detected gcc.exe
C++ Compiler: Detected c++.exe
Debugger:     Bundled GDB
```

## CMake options

You do not need any custom CMake options.

Remove any previous:

```text
-DCMAKE_PREFIX_PATH=...
```

from the CMake profile.

## Build

Reload CMake.

You should see:

```text
-- Configuring done
-- Generating done
```

Then run the `NewtTechLauncher` target.

The EXE will be produced under the normal CLion build folder, for example:

```text
cmake-build-debug/NewtTechLauncher.exe
```

It should run directly without Qt DLLs.

## Server connection

Open:

```text
src/AppConfig.h
```

and change:

```cpp
inline constexpr wchar_t INDEX_URL[] =
    L"https://YOUR-DOMAIN-HERE/modpacks/index.json";
```

to your real index endpoint when ready.

Until then, the launcher will open and show a styled server-connection error.

## Current features

- modern dark launcher shell
- sidebar navigation
- remote `index.json` download
- dynamic enabled-pack list
- featured/default pack selection
- remote `manifest.json` loading
- pack version display
- Minecraft version display
- loader/version display
- server address display
- managed-file count
- loading/status display
- error handling
- Refresh button
- placeholder Install/Play and Verify/Repair buttons

## Important

This project is Windows-only by design.

That is intentional so we can use the Windows platform APIs already present on every target machine and eliminate the external Qt toolchain/runtime problem entirely.
