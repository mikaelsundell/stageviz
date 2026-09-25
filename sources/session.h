// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include "stageviz.h"
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
class ViewState;
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
        /**
         * @brief Fully load the stage, including payloads.
         */
        All,
        /**
         * @brief Open the stage without loading payloads.
         */
        None
    };

    /**
     * @brief Progress block state.
     */
    enum ProgressMode {
        /**
         * @brief No progress operation running.
         */
        Idle,
        /**
         * @brief Progress operation active.
         */
        Running
    };

    /**
     * @brief Controls how prim changes are propagated.
     */
    enum PrimsUpdate {
        /**
         * @brief Emit prim changes immediately.
         */
        Immediate,
        /**
         * @brief Buffer and emit changes on flush.
         */
        Deferred
    };

    /**
     * @brief Stage loading status.
     */
    enum StageStatus {
        /**
         * @brief Stage successfully loaded.
         */
        Loaded,
        /**
         * @brief Stage loading failed.
         */
        Failed,
        /**
         * @brief Stage has been closed.
         */
        Closed
    };

    /**
     * @brief Stage up axis.
     */
    enum StageUp {
        /**
         * @brief Y-up.
         */
        Y,
        /**
         * @brief Z-up.
         */
        Z,
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
        enum class Status { Success, Progress, Warning, Error };

        /**
         * @brief Notification message.
         */
        QString message;
        /**
         * @brief Associated prim paths.
         */
        QList<SdfPath> paths;
        /**
         * @brief Additional metadata.
         */
        QVariantMap details;
        /**
         * @brief Notification severity.
         */
        Status status = Status::Success;

        Notify() = default;

        Notify(const QString& msg, const QList<SdfPath>& p = {}, Status s = Status::Success, const QVariantMap& d = {})
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

    /**
     * @name Progress Reporting
     */
    /**
     * @{
     */

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

    /**
     * @}
     */

    /**
     * @name Stage Operations
     */
    /**
     * @{
     */

    /**
     * @brief Creates a new empty USD stage in memory.
     *
     * Clears any existing stage and initializes a fresh one. The requested
     * load policy is also applied to the stage load rules so payloads authored
     * later follow the same All/None behavior as stages opened from disk.
     *
     * @param policy Payload loading policy for the new stage.
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
     * @brief Recomputes derived stage state and emits a full prim refresh.
     *
     * Used by undoable commands that restore edit-layer content directly.
     */
    void refreshStage();

    /**
     * @brief Saves the root layer and modified file-backed local sublayers.
     *
     * Backing layers use USD's save path and retain a recovery export on write
     * failure. Earlier successful layer writes are not rolled back. Save As
     * anchors root-layer asset paths to their original location. Anonymous
     * sublayers require a filename first.
     *
     * @param filename Destination root-layer filename.
     * @return True if all required layer and optional session-state writes succeed.
     */
    bool saveToFile(const QString& filename);

    /**
     * @brief Exports a root-layer copy without changing the current filename.
     *
     * Asset paths retain their original anchors. Dirty sublayers are not saved.
     * @param filename Destination for the root-layer copy.
     * @return True if the copy and optional session-state write succeed.
     */
    bool copyToFile(const QString& filename);

    /**
     * @brief Flattens the entire stage to a file.
     */
    bool flattenToFile(const QString& filename);

    /**
     * @brief Exports the composed content of selected prim hierarchies.
     *
     * The export preserves session-layer opinions, muted layers, resolver
     * context, and payload load rules. Population expands to include dependencies.
     * @param paths Root prim paths to include; an empty selection fails.
     * @param filename Destination flattened USD file.
     * @return True if export succeeds.
     */
    bool flattenPathsToFile(const QList<SdfPath>& paths, const QString& filename);

    /**
     * @brief Loads state from file.
     */
    bool loadState(const QString& filename);

    /**
     * @brief Saves state to file.
     */
    bool saveState(const QString& filename);

    /**
     * @brief Enables preservation of state.
     */
    void setPreserveState(bool enabled);

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

    /**
     * @}
     */

    /**
     * @name Auxiliary Stage
     */
    /**
     * @{
     */

    /**
     * @brief Returns the Stageviz-owned auxiliary USD stage.
     *
     * The auxiliary stage is an in-memory stage kept separate from the
     * primary document stage returned by stage(). It is intended for
     * Stageviz-owned scene content such as override materials, guides,
     * helpers, diagnostics, manipulators, and other non-document data.
     *
     * Content on the auxiliary stage is not part of the document layer stack
     * and is therefore not included in normal document save, export, outliner,
     * bounding-box, or change-tracking operations.
     */
    UsdStageRefPtr auxiliary() const;

    /**
     * @brief Returns the auxiliary USD stage without acquiring the auxiliary lock.
     *
     * The caller must already hold auxiliaryLock().
     */
    UsdStageRefPtr auxiliaryUnsafe() const;

    /**
     * @brief Returns the lock used for thread-safe access to the auxiliary stage.
     */
    QReadWriteLock* auxiliaryLock() const;

    /**
     * @}
     */

    /**
     * @name Scene State
     */
    /**
     * @{
     */

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
     * @brief Sets the Stageviz edit layer.
     *
     * Only layers in the stage's local layer stack are accepted. The stage
     * creates the corresponding local-layer edit target so layer offsets are
     * preserved correctly. CommandStack tracks edit-target changes so undoable
     * edit-layer commands can participate in history while external changes
     * invalidate stale history safely.
     *
     * @param layer Local layer to use for subsequent Stageviz authoring.
     * @return True if the layer is valid and became the active edit layer.
     */
    bool setEditLayer(const SdfLayerHandle& layer);

    /**
     * @brief Returns the stage lock used for thread-safe access.
     */
    QReadWriteLock* stageLock() const;

    /**
     * @}
     */

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
     * @name Command State
     */
    /**
     * @{
     */

    /**
     * @brief Returns the command stack used for undo and redo.
     */
    CommandStack* commandStack() const;

    /**
     * @}
     */

    /**
     * @name View State
     */
    /**
     * @{
     */

    /**
     * @brief Returns the shared selection state.
     */
    SelectionList* selectionList() const;

    /**
     * @brief Returns the shared viewport state.
     */
    ViewState* viewState() const;

    /**
     * @}
     */

    /**
     * @brief Requests a redraw of views connected to this session.
     */
    void notifyRedraw();

    /**
     * @brief Sets a textual status message.
     */
    void notifyStatus(Notify::Status status, const QString& message, const QString& details = QString());

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
     * @brief Emitted when the active Stageviz edit layer changes.
     *
     * Changing the edit layer changes where future authoring is written but
     * does not itself modify the composed USD stage.
     */
    void editLayerChanged(SdfLayerHandle layer);

    /**
     * @brief Emitted when the Stageviz-owned auxiliary USD stage changes.
     */
    void auxiliaryChanged(UsdStageRefPtr auxiliary);

    /**
     * @brief Emitted when the stage up axis changes.
     */
    void stageUpChanged(StageUp stageUp);

    /**
     * @brief Emitted preservation of state changes.
     */
    void preserveStateChanged(bool enabled);

    /**
     * @brief Emitted when a redraw is requested.
     */
    void redrawRequested();

    /**
     * @brief Emitted when the status message changes.
     */
    void notifyStatusChanged(Notify::Status status, const QString& message, const QString& details);

private:
    QExplicitlySharedDataPointer<SessionPrivate> p;
};

}  // namespace stageviz
