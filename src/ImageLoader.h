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

    static HBITMAP loadFromUrlPreserveAspect(
        const std::wstring& url,
        int maxWidth,
        int maxHeight
    );

    static HBITMAP loadFromFilePreserveAspect(
        const std::wstring& path,
        int maxWidth,
        int maxHeight
    );
};
