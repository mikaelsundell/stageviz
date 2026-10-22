// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "githubclient.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

namespace stageviz {

class GithubClientPrivate : public QObject {
    Q_OBJECT
public:
    GithubClientPrivate();
    ~GithubClientPrivate();
    void init();
    void fetchReleases();
    void handleReply(QNetworkReply* reply);
    QList<Github::Asset> parseAssets(const QJsonArray& array);
    Github::Release parseRelease(const QJsonObject& obj);
    struct Data {
        QString owner;
        QString repository;
        QNetworkAccessManager* network;
        QPointer<GithubClient> object;
    };
    Data d;
};

GithubClientPrivate::GithubClientPrivate() {}

GithubClientPrivate::~GithubClientPrivate()
{
    if (d.network)
        d.network->disconnect();
}

void
GithubClientPrivate::init()
{
    d.network = new QNetworkAccessManager(this);
}

void
GithubClientPrivate::fetchReleases()
{
    if (d.owner.isEmpty() || d.repository.isEmpty()) {
        if (d.object)
            d.object->errorOccurred(QStringLiteral("repository not set"));
        return;
    }
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases").arg(d.owner, d.repository));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GithubClient"));

    auto* reply = d.network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void
GithubClientPrivate::handleReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (!d.object)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT d.object->errorOccurred(reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isArray()) {
        Q_EMIT d.object->errorOccurred(QStringLiteral("Invalid GitHub API response"));
        return;
    }
    QList<Github::Release> releases;
    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;

        releases.append(parseRelease(value.toObject()));
    }
    Q_EMIT d.object->releasesReceived(releases);
}

Github::Release
GithubClientPrivate::parseRelease(const QJsonObject& obj)
{
    Github::Release release;
    release.tag = obj.value("tag_name").toString();
    release.title = obj.value("name").toString();
    release.notes = obj.value("body").toString();
    release.url = QUrl(obj.value("html_url").toString());
    release.published = QDateTime::fromString(obj.value("published_at").toString(), Qt::ISODate);
    release.assets = parseAssets(obj.value("assets").toArray());
    return release;
}

QList<Github::Asset>
GithubClientPrivate::parseAssets(const QJsonArray& array)
{
    QList<Github::Asset> assets;
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;

        const QJsonObject obj = value.toObject();
        Github::Asset asset;
        asset.name = obj.value("name").toString();
        asset.url = QUrl(obj.value("browser_download_url").toString());
        assets.append(std::move(asset));
    }
    return assets;
}

GithubClient::GithubClient(QObject* parent)
    : QObject(parent)
    , p(new GithubClientPrivate())
{
    p->d.object = this;
    p->init();
}

GithubClient::~GithubClient() {}

void
GithubClient::setRepository(const QString& owner, const QString& repository)
{
    p->d.owner = owner;
    p->d.repository = repository;
    p->fetchReleases();
}

void
GithubClient::setUrl(const QString& url)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.host().toLower() != "github.com") {
        Q_EMIT errorOccurred(tr("Invalid GitHub URL: %1").arg(url));
        return;
    }
    const QStringList parts = parsed.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        Q_EMIT errorOccurred(tr("GitHub URL does not contain owner and repository: %1").arg(url));
        return;
    }
    p->d.owner = parts.at(0);
    p->d.repository = parts.at(1);
    p->fetchReleases();
}

}  // namespace stageviz

#include "githubclient.moc"