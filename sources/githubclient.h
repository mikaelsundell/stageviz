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
     * @struct Asset
     * @brief Downloadable asset attached to a GitHub release.
     */
    struct Asset {
        /**
         * @brief Asset file name.
         */
        QString name;
        /**
         * @brief Browser/download URL for the asset.
         */
        QUrl url;
    };

    /**
     * @struct Release
     * @brief GitHub release metadata.
     */
    struct Release {
    public:
        /**
         * @brief Release tag name.
         */
        QString tag;
        /**
         * @brief Release title/name.
         */
        QString title;
        /**
         * @brief Release notes/body text.
         */
        QString notes;
        /**
         * @brief Browser URL for the release.
         */
        QUrl url;
        /**
         * @brief Release publish timestamp.
         */
        QDateTime published;
        /**
         * @brief Assets attached to the release.
         */
        QList<Asset> assets;
    };

}  // namespace Github

class GithubClientPrivate;

/**
 * @class GithubClient
 * @brief Small QObject client for fetching GitHub release information.
 */
class GithubClient : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Creates the asynchronous GitHub request client.
     */
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
