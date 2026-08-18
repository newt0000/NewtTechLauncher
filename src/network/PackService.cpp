#include "PackService.h"

#include "../AppConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

PackService::PackService(QObject* parent)
    : QObject(parent)
{
}

void PackService::fetchIndex()
{
    emit statusChanged(QStringLiteral("Connecting to pack server..."));

    const QUrl url(AppConfig::INDEX_URL);

    if (!url.isValid() || url.scheme().isEmpty())
    {
        emit errorOccurred(
            QStringLiteral("Invalid launcher URL"),
            QStringLiteral("AppConfig::INDEX_URL is not a valid URL.")
        );
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("NewtTechLauncher/%1").arg(AppConfig::VERSION)
    );

    QNetworkReply* reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]()
    {
        if (reply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred(
                QStringLiteral("Pack server unavailable"),
                QStringLiteral(
                    "Unable to download index.json.\n\n%1\n\n"
                    "Check AppConfig.h and verify the file is publicly reachable."
                ).arg(reply->errorString())
            );

            emit statusChanged(QStringLiteral("Pack server unavailable"));
            reply->deleteLater();
            return;
        }

        try
        {
            QString launcherVersion;
            QVector<Modpack> packs = parseIndex(reply->readAll(), launcherVersion);

            emit indexLoaded(packs, launcherVersion);
            emit statusChanged(
                QStringLiteral("Online • %1 pack%2 available")
                    .arg(packs.size())
                    .arg(packs.size() == 1 ? QString() : QStringLiteral("s"))
            );
        }
        catch (const std::exception& ex)
        {
            emit errorOccurred(
                QStringLiteral("Invalid index.json"),
                QString::fromUtf8(ex.what())
            );

            emit statusChanged(QStringLiteral("Invalid pack index"));
        }

        reply->deleteLater();
    });
}

void PackService::fetchManifest(const Modpack& pack)
{
    emit statusChanged(QStringLiteral("Loading %1...").arg(pack.name));

    QUrl url(pack.manifestUrl);

    if (!url.isValid() || url.scheme().isEmpty())
    {
        emit errorOccurred(
            QStringLiteral("Invalid manifest URL"),
            QStringLiteral("The manifest URL for %1 is invalid.").arg(pack.name)
        );
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("NewtTechLauncher/%1").arg(AppConfig::VERSION)
    );

    QNetworkReply* reply = m_network.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, pack]()
    {
        if (reply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred(
                QStringLiteral("Manifest unavailable"),
                QStringLiteral("Could not load %1.\n\n%2")
                    .arg(pack.name, reply->errorString())
            );

            emit statusChanged(QStringLiteral("Manifest unavailable"));
            reply->deleteLater();
            return;
        }

        try
        {
            PackManifest manifest = parseManifest(reply->readAll());

            emit manifestLoaded(pack, manifest);
            emit statusChanged(QStringLiteral("Connected • manifest loaded"));
        }
        catch (const std::exception& ex)
        {
            emit errorOccurred(
                QStringLiteral("Invalid manifest.json"),
                QString::fromUtf8(ex.what())
            );

            emit statusChanged(QStringLiteral("Invalid pack manifest"));
        }

        reply->deleteLater();
    });
}

QVector<Modpack> PackService::parseIndex(
    const QByteArray& data,
    QString& launcherVersion)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError)
        throw std::runtime_error(
            QStringLiteral("JSON parse error: %1")
                .arg(error.errorString())
                .toStdString()
        );

    if (!document.isObject())
        throw std::runtime_error("index.json must contain a JSON object.");

    const QJsonObject root = document.object();
    launcherVersion = root.value(QStringLiteral("launcherVersion")).toString();

    const QJsonValue packsValue = root.value(QStringLiteral("packs"));

    if (!packsValue.isArray())
        throw std::runtime_error("index.json is missing the packs array.");

    QVector<Modpack> packs;

    for (const QJsonValue& value : packsValue.toArray())
    {
        if (!value.isObject())
            continue;

        const QJsonObject object = value.toObject();

        Modpack pack;
        pack.id = object.value(QStringLiteral("id")).toString();
        pack.name = object.value(QStringLiteral("name")).toString();
        pack.description = object.value(QStringLiteral("description")).toString();
        pack.iconUrl = object.value(QStringLiteral("icon")).toString();
        pack.bannerUrl = object.value(QStringLiteral("banner")).toString();
        pack.manifestUrl = object.value(QStringLiteral("manifest")).toString();
        pack.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        pack.featured = object.value(QStringLiteral("featured")).toBool(false);
        pack.accent = object.value(QStringLiteral("accent"))
                          .toString(QStringLiteral("#F47A21"));

        if (pack.enabled)
            packs.push_back(pack);
    }

    return packs;
}

PackManifest PackService::parseManifest(const QByteArray& data)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError)
        throw std::runtime_error(
            QStringLiteral("JSON parse error: %1")
                .arg(error.errorString())
                .toStdString()
        );

    if (!document.isObject())
        throw std::runtime_error("manifest.json must contain a JSON object.");

    const QJsonObject root = document.object();

    PackManifest manifest;
    manifest.id = root.value(QStringLiteral("id")).toString();
    manifest.name = root.value(QStringLiteral("name")).toString();
    manifest.version = root.value(QStringLiteral("version")).toString();

    const QJsonObject minecraft =
        root.value(QStringLiteral("minecraft")).toObject();

    manifest.minecraft.version =
        minecraft.value(QStringLiteral("version")).toString();

    manifest.minecraft.loader =
        minecraft.value(QStringLiteral("loader")).toString();

    manifest.minecraft.loaderVersion =
        minecraft.value(QStringLiteral("loaderVersion")).toString();

    const QJsonObject java =
        root.value(QStringLiteral("java")).toObject();

    manifest.java.minimumVersion =
        java.value(QStringLiteral("minimumVersion")).toInt(17);

    manifest.java.recommendedMemory =
        java.value(QStringLiteral("recommendedMemory")).toInt(8192);

    const QJsonObject server =
        root.value(QStringLiteral("server")).toObject();

    manifest.server.address =
        server.value(QStringLiteral("address")).toString();

    const QJsonArray files =
        root.value(QStringLiteral("files")).toArray();

    for (const QJsonValue& value : files)
    {
        if (!value.isObject())
            continue;

        const QJsonObject object = value.toObject();

        PackFile file;
        file.path = object.value(QStringLiteral("path")).toString();
        file.url = object.value(QStringLiteral("url")).toString();
        file.size =
            static_cast<qint64>(
                object.value(QStringLiteral("size")).toDouble(0)
            );

        file.sha256 =
            object.value(QStringLiteral("sha256")).toString();

        file.policy =
            object.value(QStringLiteral("policy"))
                .toString(QStringLiteral("replace"));

        manifest.files.push_back(file);
    }

    return manifest;
}
