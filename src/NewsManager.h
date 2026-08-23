#pragma once

#include <string>
#include <vector>

struct NewsItem
{
    std::wstring id;
    std::wstring title;
    std::wstring summary;
    std::wstring body;
    std::wstring imageUrl;
    std::wstring published;
    std::wstring category;
    std::wstring url;

    bool pinned = false;
    bool visible = true;
};

class NewsManager
{
public:
    static std::vector<NewsItem> fetch();

private:
    static std::wstring utf8ToWide(
        const std::string& value
    );
};
