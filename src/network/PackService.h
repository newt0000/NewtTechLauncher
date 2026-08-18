#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QVector>

#include "../models/Modpack.h"
#include "../models/PackManifest.h"

class PackService final : public QObject
{
    Q_OBJECT

public:
    explicit PackService(QObject* parent = nullptr);

    void fetchIndex();
    void fetchManifest(const Modpack& pack);

signals:
    void indexLoaded(const QVector<Modpack>& packs, const QString& launcherVersion);
    void manifestLoaded(const Modpack& pack, const PackManifest& manifest);

    void statusChanged(const QString& status);
    void errorOccurred(const QString& title, const QString& message);

private:
    QNetworkAccessManager m_network;

    static QVector<Modpack> parseIndex(const QByteArray& data, QString& launcherVersion);
    static PackManifest parseManifest(const QByteArray& data);
};
