cmake_minimum_required(VERSION 3.21)

project(NewtTechLauncher
        VERSION 0.4.0
        DESCRIPTION "NewtTech Modpack Launcher"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(NewtTechLauncher WIN32
        src/main.cpp
        src/AppConfig.h
        src/Models.h
        src/HttpClient.h
        src/HttpClient.cpp
        src/JsonLite.h
        src/JsonLite.cpp
        src/ImageLoader.h
        src/ImageLoader.cpp
        src/Settings.h
        src/Settings.cpp
        src/InstallEngine.h
        src/InstallEngine.cpp
        src/MinecraftProfile.h
        src/MinecraftProfile.cpp
        src/VersionManager.h
        src/VersionManager.cpp
        src/MainWindow.h
        src/MainWindow.cpp
)

target_compile_definitions(NewtTechLauncher PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(NewtTechLauncher PRIVATE
        winhttp
        user32
        gdi32
        comctl32
        shell32
        ole32
        windowscodecs
        urlmon
        bcrypt
        crypt32
        dwmapi
)


if(MINGW)
    target_link_options(NewtTechLauncher PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
    )
endif()


add_executable(NewtTechInstaller WIN32
        installer/Installer.c
)

target_compile_definitions(NewtTechInstaller PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(NewtTechInstaller PRIVATE
        winhttp
        user32
        gdi32
        shell32
        ole32
        uuid
        bcrypt
        advapi32
        dwmapi
)

# Installer is intentionally pure Win32 C. It does not link libstdc++ or winpthread.

