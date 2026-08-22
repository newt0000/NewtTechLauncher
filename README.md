# NewtTech Launcher

```{=html}
<p align="center">
```
`<img src="assets/launcher-logo.png" alt="NewtTech Launcher" width="180">`{=html}
```{=html}
</p>
```
```{=html}
<p align="center">
```
`<strong>`{=html}A lightweight Windows launcher for managed Minecraft
modpacks.`</strong>`{=html}
```{=html}
</p>
```
```{=html}
<p align="center">
```
`<a href="https://github.com/newt0000/NewtTechLauncher/releases">`{=html}
`<img src="https://img.shields.io/github/v/release/newt0000/NewtTechLauncher?style=for-the-badge&label=Release" alt="Latest Release">`{=html}
`</a>`{=html}
`<a href="https://github.com/newt0000/NewtTechLauncher/releases">`{=html}
`<img src="https://img.shields.io/github/downloads/newt0000/NewtTechLauncher/total?style=for-the-badge&label=GitHub%20Downloads" alt="GitHub Downloads">`{=html}
`</a>`{=html}
`<img src="https://img.shields.io/badge/Platform-Windows-0078D4?style=for-the-badge" alt="Windows">`{=html}
`<img src="https://img.shields.io/badge/Language-C%2B%2B-00599C?style=for-the-badge" alt="C++">`{=html}
`<a href="LICENSE">`{=html}
`<img src="https://img.shields.io/badge/License-CC0--1.0-44CC11?style=for-the-badge" alt="CC0-1.0">`{=html}
`</a>`{=html}
```{=html}
</p>
```

------------------------------------------------------------------------

## About NewtTech Launcher

**NewtTech Launcher** is a native Windows launcher designed to make
installing, maintaining, and launching managed Minecraft modpacks
simple.

Instead of asking players to manually download mods, configure Forge,
create Minecraft installations, and maintain matching mod versions,
NewtTech Launcher handles the process automatically from centrally
managed pack manifests.

Select a pack, install it, and launch it through the official Minecraft
Launcher.

```{=html}
<p align="center">
```
`<img src="assets/screenshots/modpacks.png" alt="NewtTech Launcher Modpacks" width="900">`{=html}
```{=html}
</p>
```

------------------------------------------------------------------------

## Features

### Managed Modpacks

NewtTech Launcher retrieves the available modpack catalog from the
NewtTech launcher service and presents packs through a native Windows
interface.

Each pack can provide:

-   Pack name and description
-   Custom icon
-   Custom banner
-   Pack version
-   Minecraft version
-   Forge version
-   Server information
-   Managed file list
-   Installation metadata

Pack artwork supports PNG transparency and is displayed directly inside
the launcher interface.

### Automatic Installation

Installing a pack automatically creates and manages its own instance
directory.

The launcher can download:

-   Mods
-   Configuration files
-   Pack resources
-   Required managed files
-   Minecraft version metadata
-   Forge version files

Files are downloaded according to the server-provided pack manifest,
removing the need for players to manually assemble the modpack.

```{=html}
<p align="center">
```
`<img src="assets/screenshots/install.png" alt="NewtTech Launcher installation" width="900">`{=html}
```{=html}
</p>
```
### Minecraft Launcher Integration

NewtTech Launcher works alongside the **official Minecraft Launcher**
rather than replacing Minecraft authentication or account management.

When a pack is prepared, NewtTech Launcher can automatically create a
Minecraft installation profile configured with:

-   The required Minecraft version
-   The required Forge version
-   The pack's dedicated instance directory
-   The pack's custom icon

The official Minecraft Launcher can then be opened with the installation
ready to use.

### Automatic Forge Provisioning

Players do not need to manually install the Forge version required by a
managed pack.

NewtTech Launcher can retrieve the appropriate Forge version package
from the launcher service and provision it into:

``` text
.minecraft/versions/
```

The generated Minecraft installation profile is then configured to use
that version.

### Verify & Repair

Installed packs can be checked using **Verify / Repair**.

The launcher compares the managed installation against the pack manifest
and can restore files that are:

-   Missing
-   Damaged
-   Outdated
-   Incorrect

This provides a simple recovery option when a modpack stops working
without requiring a complete manual reinstall.

### Pack Updates

Pack contents are controlled through remote manifests. The **Refresh**
function retrieves current pack metadata without requiring the launcher
to be reinstalled.

### Dedicated Instances

Each modpack receives its own managed instance directory, keeping
pack-specific files separate from the player's primary Minecraft
installation and allowing multiple managed packs to coexist.

### Downloads

The Downloads section provides visibility into launcher-managed
downloads and installation activity.

```{=html}
<p align="center">
```
`<img src="assets/screenshots/downloads.png" alt="NewtTech Launcher Downloads" width="900">`{=html}
```{=html}
</p>
```
### Settings

Launcher preferences and configurable behavior are available from the
dedicated Settings section.

```{=html}
<p align="center">
```
`<img src="assets/screenshots/settings.png" alt="NewtTech Launcher Settings" width="900">`{=html}
```{=html}
</p>
```

------------------------------------------------------------------------

## Native Windows Interface

NewtTech Launcher is built as a native Windows application.

The interface includes:

-   Custom NewtTech window frame
-   Resizable window
-   Custom title bar
-   Native minimize, maximize, and close controls
-   Modern dark navy interface
-   Cyan and magenta accents
-   Pack cards
-   Transparent PNG artwork
-   Dynamic layouts
-   Custom pack banners and icons

The launcher does not require an embedded browser to render its primary
interface.

------------------------------------------------------------------------

## Launcher Workflow

``` text
NewtTech Launcher
        │
        ▼
Download Pack Manifest
        │
        ▼
Select Modpack
        │
        ├───────────────┐
        ▼               ▼
     Install       Verify / Repair
        │               │
        └───────┬───────┘
                ▼
       Prepare Instance
                │
                ▼
       Provision Forge
                │
                ▼
 Create Minecraft Profile
                │
                ▼
 Open Minecraft Launcher
                │
                ▼
              Play
```

------------------------------------------------------------------------

## Installation

### Recommended

Download the latest **NewtTech Installer** from the official project
release.

[![Download NewtTech
Launcher](https://img.shields.io/badge/Download-NewtTech%20Launcher-ff1493?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/newt0000/NewtTechLauncher/releases/latest)

Run:

``` text
NewtTechInstaller.exe
```

The installer handles installation of the launcher and its required
components, creates the uninstall entry, and can create shortcuts for
convenient access.

------------------------------------------------------------------------

## System Requirements

Component           Requirement
  ------------------- ---------------------------------------------
Operating System    Windows 10 / Windows 11
Architecture        x64
Minecraft           Official Minecraft Launcher installed
Network             Internet connection
Storage             Depends on installed modpacks
Minecraft Account   Required by the official Minecraft Launcher

Individual modpacks may have additional memory, GPU, storage, or
Minecraft requirements.

------------------------------------------------------------------------

## Building From Source

NewtTech Launcher uses **CMake** and a MinGW-compatible toolchain.

### Requirements

-   CMake
-   Ninja
-   MinGW-w64 / GCC
-   Windows SDK-compatible headers and libraries
-   Git

Clone the repository:

``` bash
git clone https://github.com/newt0000/NewtTechLauncher.git
cd NewtTechLauncher
```

Configure:

``` bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build:

``` bash
cmake --build build
```

The project contains separate targets for:

``` text
NewtTechLauncher
NewtTechInstaller
```

------------------------------------------------------------------------

## Automated Builds

Official source builds can be produced using **GitHub Actions**.

The Windows build pipeline:

1.  Checks out the public repository
2.  Creates a Windows MinGW build environment
3.  Configures the project with CMake
4.  Builds `NewtTechLauncher.exe`
5.  Builds `NewtTechInstaller.exe`
6.  Packages the resulting Windows binaries as a build artifact

[![Build NewtTech
Launcher](https://github.com/newt0000/NewtTechLauncher/actions/workflows/build.yml/badge.svg)](https://github.com/newt0000/NewtTechLauncher/actions/workflows/build.yml)

------------------------------------------------------------------------

## Project Structure

``` text
NewtTechLauncher/
│
├── .github/
│   └── workflows/
│       ├── build.yml
│       └── release-sign.yml
│
├── installer/
│   └── Installer.c
│
├── src/
│   ├── MainWindow.cpp
│   ├── ImageLoader.cpp
│   ├── HttpClient.cpp
│   ├── InstallEngine.cpp
│   ├── MinecraftProfile.cpp
│   ├── VersionManager.cpp
│   ├── Settings.cpp
│   └── ...
│
├── assets/
│   ├── launcher-logo.png
│   └── screenshots/
│       ├── modpacks.png
│       ├── install.png
│       ├── downloads.png
│       └── settings.png
│
├── CMakeLists.txt
├── CODE_SIGNING.md
├── LICENSE
└── README.md
```

------------------------------------------------------------------------

## Server-Side Pack Management

NewtTech Launcher is designed around remotely managed pack manifests. A
pack definition can describe information such as:

``` json
{
  "name": "Example Pack",
  "version": "1.0.0",
  "minecraft": "1.20.1",
  "forge": "47.4.22",
  "server": "example.net:25565"
}
```

Managed files are downloaded from the configured launcher service and
installed into the appropriate instance, allowing pack administrators to
update managed content without requiring players to manually replace
individual files.

------------------------------------------------------------------------

## Artwork

Each pack can provide its own icon, banner, and Minecraft profile icon.
PNG artwork can include alpha transparency.

Example asset layout:

``` text
assets/
├── launcher-logo.png
├── screenshots/
│   ├── modpacks.png
│   ├── install.png
│   ├── downloads.png
│   └── settings.png
└── packs/
    ├── example-icon.png
    └── example-banner.png
```

------------------------------------------------------------------------

## Security

NewtTech Launcher does not replace Minecraft or Microsoft
authentication. Authentication remains handled by the official Minecraft
Launcher.

Managed content is retrieved from the configured NewtTech launcher
service over HTTPS. Official Windows builds are intended to be produced
from the public repository through the project's GitHub Actions build
pipeline.

See [`CODE_SIGNING.md`](CODE_SIGNING.md) for information about the
project's release and code-signing policy.

------------------------------------------------------------------------

## Troubleshooting

### Pack does not appear

Use **Refresh** to retrieve the latest pack manifest.

### Pack will not launch

Run **Verify / Repair** first to check the managed installation.

### Minecraft installation is missing

Make sure the official Minecraft Launcher has been installed and
launched at least once.

### Forge installation is missing

Use **Verify / Repair** or reinstall the affected pack so the required
version files can be provisioned.

### Mods are missing

Run **Verify / Repair** to compare the instance against the managed pack
manifest.

------------------------------------------------------------------------

## Development Status

NewtTech Launcher is under active development. Features, manifest
formats, server APIs, and user-interface behavior may evolve between
releases.

Bug reports and feature requests are welcome through GitHub Issues.

[![Issues](https://img.shields.io/github/issues/newt0000/NewtTechLauncher?style=for-the-badge)](https://github.com/newt0000/NewtTechLauncher/issues)

------------------------------------------------------------------------

## License

NewtTech Launcher is released under **CC0 1.0 Universal**.

See [`LICENSE`](LICENSE) for the complete license terms.

[![CC0](https://img.shields.io/badge/License-CC0%201.0-lightgrey?style=for-the-badge)](LICENSE)

------------------------------------------------------------------------

```{=html}
<p align="center">
```
`<img src="assets/launcher-logo.png" alt="NewtTech Launcher" width="96">`{=html}
```{=html}
</p>
```
```{=html}
<p align="center">
```
`<strong>`{=html}NewtTech Launcher`</strong>`{=html}`<br>`{=html}
Install. Repair. Launch.
```{=html}
</p>
```
