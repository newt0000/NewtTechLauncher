#include <windows.h>

#include "MainWindow.h"

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int showCommand)
{
    MainWindow window;

    if (!window.create(
            instance,
            showCommand))
        return 1;

    return window.run();
}
