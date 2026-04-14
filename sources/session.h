// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include <QExplicitlySharedDataPointer>
#include <QMap>
#include <QObject>
#include <QReadWriteLock>
#include <QVariant>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class CommandStack;
class SelectionList;
class SessionPrivate;

/**
 * @class Session
 * @brief Central session managing the USD stage and scene state.
 *
 * Provides access to the currently loaded USD stage and maintains
 * application state such as progress notifications, scene masks,
 * bounding boxes, and status messages. The model acts as a shared
 * data source for viewer components like the stage tree, render
 * view, and property inspector.
 *
 * Session instances use implicit sharing and thread-safe access
 * to the underlying USD stage.
 */
class Session : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Stage loading policy.
     */
    enum LoadPolicy {
        All,  ///< Fully load the stage, including payloads.
        None  ///< Open the stage without loading payloads.
    };

    /**
     * @brief Progress block state.
     */
    enum ProgressMode {
        Idle,    ///< No progress operation running.
        Running  ///< Progress operation active.
    };

    /**
     * @brief Controls how prim changes are propagated.
     */
    enum PrimsUpdate {
        Immediate,  ///< Emit prim changes immediately.
        Deferred    ///< Buffer and emit changes on flush.
    };

    /**
     * @brief Stage loading status.
     */
    enum StageStatus {
        Loaded,  ///< Stage successfully loaded.
        Failed,  ///< Stage loading failed.
        Closed   ///< Stage has been closed.
    };

    /**
     * @brief Stage up axis.
     */
    enum StageUp {
        Y,  ///< Y-up.
        Z,  ///< Z-up.
    };

public:
    /**
     * @struct Notify
     * @brief Progress notification payload.
     *
     * Used to describe updates emitted during long-running
     * operations such as stage loading or export tasks.
     */
    struct Notify {
        enum class Status { Info, Progress, Warning, Error };

        QString message;               ///< Notification message.
        QList<SdfPath> paths;          ///< Associated prim paths.
        QVariantMap details;           ///< Additional metadata.
        Status status = Status::Info;  ///< Notification severity.

        Notify() = default;

        Notify(const QString& msg, const QList<SdfPath>& p = {}, Status s = Status::Info, const QVariantMap& d = {})
            : message(msg)
            , paths(p)
            , details(d)
            , status(s)
        {}
    };

public:
    /**
     * @brief Constructs an empty session.
     */
    Session();

    /**
     * @brief Constructs and loads a stage from file.
     *
     * @param filename USD file to load.
     * @param policy Stage loading policy.
     */
    Session(const QString& filename, LoadPolicy policy = LoadPolicy::All);

    /**
     * @brief Copy constructor.
     */
    Session(const Session& other);

    /**
     * @brief Destroys the Session instance.
     */
    ~Session();

    /** @name Progress Reporting */
    ///@{

    /**
     * @brief Begins a progress block.
     *
     * @param name Name of the progress operation.
     * @param count Expected number of steps.
     */
    void beginProgressBlock(const QString& name, size_t count = 0);

    /**
     * @brief Updates progress with a notification.
     *
     * @param notify Notification payload.
     * @param completed Number of completed steps.
     */
    void updateProgressNotify(const Notify& notify, size_t completed);

    /**
     * @brief Cancels the active progress block.
     */
    void cancelProgressBlock();

    /**
     * @brief Ends the current progress block.
     */
    void endProgressBlock();

    /**
     * @brief Returns whether the progress block has been cancelled.
     */
    bool isProgressBlockCancelled() const;

    ///@}

    /** @name Stage Operations */
    ///@{

    /**
     * @brief Creates a new empty USD stage in memory.
     *
     * Clears any existing stage and initializes a fresh one.
     *
     * @return True if creation succeeded.
     */
    bool newStage(LoadPolicy policy = LoadPolicy::All);

    /**
     * @brief Loads a USD stage from file.
     *
     * @param filename File to load.
     * @param policy Stage loading policy.
     *
     * @return True if loading succeeded.
     */
    bool loadFromFile(const QString& filename, LoadPolicy policy = LoadPolicy::All);

    /**
     * @brief Merges a USD file or session state file into the current stage.
     *
     * If @p filename refers to a USD file, the file is merged into the
     * current stage and a companion ".session" file is also applied if present.
     *
     * If @p filename refers to a ".session" file, only the stored payload
     * load state is applied to the current stage.
     *
     * Unlike loadFromFile(), this does not replace the current stage.
     *
     * @param filename USD file or ".session" state file to merge.
     *
     * @return True if the merge succeeded.
     */
    bool mergeFromFile(const QString& filename);

    /**
     * @brief Saves the current stage to file.
     */
    bool saveToFile(const QString& filename);

    /**
     * @brief Copies the current stage to file.
     */
    bool copyToFile(const QString& filename);

    /**
     * @brief Flattens the entire stage to a file.
     */
    bool flattenToFile(const QString& filename);

    /**
     * @brief Flattens specific prim paths to a file.
     */
    bool flattenPathsToFile(const QList<SdfPath>& paths, const QString& filename);

    /**
     * @brief Reloads the currently opened stage.
     */
    bool reload();

    /**
     * @brief Closes the current stage.
     */
    bool close();

    /**
     * @brief Returns whether a stage is currently loaded.
     */
    bool isLoaded() const;

    ///@}

    /** @name Scene State */
    ///@{

    /**
     * @brief Returns the current mask.
     */
    QList<SdfPath> mask() const;

    /**
     * @brief Sets the active prim mask.
     */
    void setMask(const QList<SdfPath>& paths);

    /**
     * @brief Returns the current stage up axis.
     */
    StageUp stageUp();

    /**
     * @brief Sets the stage up axis.
     */
    void setStageUp(StageUp stageUp);

    /**
     * @brief Returns the current loading policy.
     */
    LoadPolicy loadPolicy() const;

    /**
     * @brief Returns the scene bounding box.
     */
    GfBBox3d boundingBox();

    /**
     * @brief Returns the current stage filename.
     */
    QString filename() const;

    /**
     * @brief Returns the active USD stage.
     */
    UsdStageRefPtr stage() const;

    /**
     * @brief Returns the active USD stage without acquiring the stage lock.
     *
     * The caller must already hold stageLock().
     */
    UsdStageRefPtr stageUnsafe() const;

    /**
     * @brief Returns the stage lock used for thread-safe access.
     */
    QReadWriteLock* stageLock() const;

    ///@}

    /** @name Session Subsystems */
    ///@{

    /**
     * @brief Returns the command stack subsystem.
     */
    CommandStack* commandStack() const;

    /**
     * @brief Returns the selection list subsystem.
     */
    SelectionList* selectionList() const;

    ///@}

    /**
     * @brief Returns the current prim update behavior.
     */
    PrimsUpdate primsUpdate() const;

    /**
     * @brief Sets how prim changes are propagated.
     *
     * Deferred buffers changes until flushed. Switching to Immediate
     * flushes pending changes.
     */
    void setPrimsUpdate(PrimsUpdate policy);

    /**
     * @brief Emits any buffered prim changes.
     */
    void flushPrimsUpdates();

    /**
     * @brief Sets a textual status message.
     */
    void notifyStatus(Notify::Status status, const QString& message);

Q_SIGNALS:
    /**
     * @brief Emitted when a progress block begins or ends.
     */
    void progressBlockChanged(const QString& name, ProgressMode mode);

    /**
     * @brief Emitted when progress updates occur.
     */
    void progressNotifyChanged(const Notify& notify, size_t completed, size_t expected);

    /**
     * @brief Emitted when the scene bounding box changes.
     */
    void boundingBoxChanged(const GfBBox3d& bbox);

    /**
     * @brief Emitted when the prim mask changes.
     */
    void maskChanged(const QList<SdfPath>& paths);

    /**
     * @brief Emitted when prims are modified using a structured USD notice batch.
     */
    void primsChanged(const NoticeBatch& batch);

    /**
     * @brief Emitted when the stage changes.
     */
    void stageChanged(UsdStageRefPtr stage, LoadPolicy policy, StageStatus status);

    /**
     * @brief Emitted when the stage up axis changes.
     */
    void stageUpChanged(StageUp stageUp);

    /**
     * @brief Emitted when the status message changes.
     */
    void notifyStatusChanged(Notify::Status status, const QString& message);

private:
    QExplicitlySharedDataPointer<SessionPrivate> p;
};

}  // namespace stageviz
