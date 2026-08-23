#include "NewsManager.h"

#include "AppConfig.h"
#include "HttpClient.h"
#include "JsonLite.h"

#include <windows.h>

#include <algorithm>

std::vector<NewsItem> NewsManager::fetch()
{
    JsonValue root =
        JsonLite::parse(
            HttpClient::getUtf8(
                AppConfig::NEWS_URL
            )
        );

    std::vector<NewsItem> result;

    for (const JsonValue& value :
         root.get("items").asArray())
    {
        NewsItem item;

        item.id =
            utf8ToWide(
                value.get("id").asString()
            );

        item.title =
            utf8ToWide(
                value.get("title").asString()
            );

        item.summary =
            utf8ToWide(
                value.get("summary").asString()
            );

        item.body =
            utf8ToWide(
                value.get("body").asString()
            );

        item.imageUrl =
            utf8ToWide(
                value.get("image").asString()
            );

        item.published =
            utf8ToWide(
                value.get("published").asString()
            );

        item.category =
            utf8ToWide(
                value.get("category").asString("News")
            );

        item.url =
            utf8ToWide(
                value.get("url").asString()
            );

        item.pinned =
            value.get("pinned").asBool(false);

        item.visible =
            value.get("visible").asBool(true);

        if (
            item.visible &&
            !item.title.empty()
        )
        {
            result.push_back(
                std::move(item)
            );
        }
    }

    std::stable_sort(
        result.begin(),
        result.end(),
        [](const NewsItem& a, const NewsItem& b)
        {
            if (a.pinned != b.pinned)
                return a.pinned > b.pinned;

            return
                a.published >
                b.published;
        }
    );

    return result;
}

std::wstring NewsManager::utf8ToWide(
    const std::string& value)
{
    if (value.empty())
        return {};

    const int length =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );

    std::wstring result(
        length,
        L'\0'
    );

    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length
    );

    return result;
}
