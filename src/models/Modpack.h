#pragma once

#include <QString>

struct Modpack
{
    QString id;
    QString name;
    QString description;
    QString iconUrl;
    QString bannerUrl;
    QString manifestUrl;
    QString accent = QStringLiteral("#F47A21");
    bool enabled = true;
    bool featured = false;
};
