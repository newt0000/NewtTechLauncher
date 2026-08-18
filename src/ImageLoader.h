#pragma once

#include <windows.h>
#include <string>

class ImageLoader
{
public:
    static HBITMAP loadFromUrl(
        const std::wstring& url,
        int width,
        int height
    );
};
