#pragma once

#include <QString>
#include <QVector>

struct MinecraftInfo
{
    QString version;
    QString loader;
    QString loaderVersion;
};

struct JavaInfo
{
    int minimumVersion = 17;
    int recommendedMemory = 8192;
};

struct ServerInfo
{
    QString address;
};

struct PackFile
{
    QString path;
    QString url;
    qint64 size = 0;
    QString sha256;
    QString policy = QStringLiteral("replace");
};

struct PackManifest
{
    QString id;
    QString name;
    QString version;
    MinecraftInfo minecraft;
    JavaInfo java;
    ServerInfo server;
    QVector<PackFile> files;
};
