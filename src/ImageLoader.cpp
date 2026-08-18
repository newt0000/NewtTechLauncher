#include "ImageLoader.h"

#include <objbase.h>
#include <urlmon.h>
#include <wincodec.h>

HBITMAP ImageLoader::loadFromUrl(
    const std::wstring& url,
    int width,
    int height)
{
    if (
        url.empty() ||
        width <= 0 ||
        height <= 0
    )
        return nullptr;

    wchar_t cachePath[MAX_PATH]{};

    HRESULT hr =
        URLDownloadToCacheFileW(
            nullptr,
            url.c_str(),
            cachePath,
            MAX_PATH,
            0,
            nullptr
        );

    if (FAILED(hr))
        return nullptr;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;

    HBITMAP bitmap = nullptr;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );

    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(
            cachePath,
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder
        );

    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);

    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapScaler(&scaler);

    if (SUCCEEDED(hr))
        hr = scaler->Initialize(
            frame,
            static_cast<UINT>(width),
            static_cast<UINT>(height),
            WICBitmapInterpolationModeFant
        );

    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);

    if (SUCCEEDED(hr))
        hr = converter->Initialize(
            scaler,
            GUID_WICPixelFormat32bppBGR,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        );

    if (SUCCEEDED(hr))
    {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;

        HDC screen = GetDC(nullptr);

        bitmap = CreateDIBSection(
            screen,
            &bmi,
            DIB_RGB_COLORS,
            &bits,
            nullptr,
            0
        );

        ReleaseDC(nullptr, screen);

        if (bitmap && bits)
        {
            const UINT stride =
                static_cast<UINT>(
                    width * 4
                );

            const UINT size =
                stride *
                static_cast<UINT>(
                    height
                );

            hr = converter->CopyPixels(
                nullptr,
                stride,
                size,
                static_cast<BYTE*>(bits)
            );

            if (FAILED(hr))
            {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
        }
    }

    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    return bitmap;
}
