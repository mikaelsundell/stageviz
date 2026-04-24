// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QDateTime>
#include <QObject>
#include <QUrl>

namespace stageviz {

/**
 * @brief Types returned by GithubClient.
 */
namespace Github {

    /**
 * @brief Downloadable asset attached to a GitHub release.
 */
    struct Asset {
        QString name;  ///< Asset file name.
        QUrl url;      ///< Browser/download URL for the asset.
    };

    /**
 * @brief GitHub release metadata.
 */
    struct Release {
    public:
        QString tag;          ///< Release tag name.
        QString title;        ///< Release title/name.
        QString notes;        ///< Release notes/body text.
        QUrl url;             ///< Browser URL for the release.
        QDateTime published;  ///< Release publish timestamp.
        QList<Asset> assets;  ///< Assets attached to the release.
    };

}  // namespace Github

class GithubClientPrivate;

/**
 * @brief Small QObject client for fetching GitHub release information.
 */
class GithubClient : public QObject {
    Q_OBJECT

public:
    explicit GithubClient(QObject* parent = nullptr);
    virtual ~GithubClient();

    /**
     * @brief Sets the GitHub repository to query.
     * @param owner Repository owner or organization.
     * @param repository Repository name.
     */
    void setRepository(const QString& owner, const QString& repository);

    /**
     * @brief Sets the project URL or repository identifier.
     */
    void setUrl(const QString& project);

Q_SIGNALS:
    /**
     * @brief Emitted when releases were fetched successfully.
     */
    void releasesReceived(const QList<Github::Release>& releases);

    /**
     * @brief Emitted when a request or parsing error occurred.
     */
    void errorOccurred(const QString& error);

private:
    QScopedPointer<GithubClientPrivate> p;
};

}  // namespace stageviz