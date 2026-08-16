// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "session.h"
#include "commandstack.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewstate.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <pxr/base/tf/weakBase.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xform.h>
#include <stack>

namespace stageviz {
class SessionPrivate : public QSharedData {
public:
    SessionPrivate();
    ~SessionPrivate();
    void init();
    void initStage();
    void beginProgressBlock(const QString& name, size_t count);
    void updateProgressNotify(const Session::Notify& notify, size_t completed);
    void cancelProgressBlock();
    void endProgressBlock();
    bool isProgressBlockCancelled() const;
    bool newStage(Session::LoadPolicy policy);
    bool loadFromFile(const QString& filename, Session::LoadPolicy loadPolicy);
    bool mergeFromFile(const QString& filename);
    bool saveToFile(const QString& filename);
    bool copyToFile(const QString& filename);
    bool flattenPathsToFile(const QList<SdfPath>& paths, const QString& filename);
    bool loadState(const QString& filename);
    bool saveState(const QString& filename);
    bool close();
    bool reload();
    bool isLoaded() const;
    void setMask(const QList<SdfPath>& paths);
    void setPayloads(const QList<SdfPath>& paths, bool loaded);
    void setPreserveState(bool enabled);
    Session::StageUp stageUp();
    void setStageUp(Session::StageUp stageUp);
    GfBBox3d boundingBox();
    bool needsBoundingBoxUpdate(const NoticeBatch& batch) const;
    void updatePrims(const NoticeBatch& batch);
    void flushPrims();
    void updateStage();

public:
    class StageWatcher : public TfWeakBase {
    public:
        explicit StageWatcher(SessionPrivate* parent) { d.parent = parent; }

        void init()
        {
            if (d.key.IsValid())
                TfNotice::Revoke(d.key);

            d.stage = nullptr;

            QMutexLocker locker(&d.pendingMutex);
            d.pending.entries.clear();
            d.dispatchQueued = false;
        }

        void watch(const UsdStageRefPtr& stage)
        {
            init();
            d.stage = stage;
            if (stage)
                d.key = TfNotice::Register(TfWeakPtr<StageWatcher>(this), &StageWatcher::objectsChanged, stage);
        }

        NoticeBatch takePending()
        {
            QMutexLocker locker(&d.pendingMutex);
            const NoticeBatch batch = d.pending;
            d.pending.entries.clear();
            d.dispatchQueued = false;
            return batch;
        }

        void dispatchPending()
        {
            const NoticeBatch batch = takePending();
            if (!batch.entries.isEmpty())
                d.parent->updatePrims(batch);
        }

        void objectsChanged(const UsdNotice::ObjectsChanged& notice, const UsdStageWeakPtr& sender)
        {
            if (d.suppress.load())
                return;

            UsdStageRefPtr senderStage = sender;
            if (!d.stage || !senderStage || d.stage != senderStage)
                return;

            NoticeBatch batch;
            for (const SdfPath& path : notice.GetChangedInfoOnlyPaths()) {
                NoticeEntry entry;
                entry.path = path;
                entry.changedInfoOnly = true;
                entry.changedFields = notice.GetChangedFields(path);
                batch.entries.append(entry);
            }
            for (const SdfPath& path : notice.GetResolvedAssetPathsResyncedPaths()) {
                NoticeEntry entry;
                entry.path = path;
                entry.resolvedAssetPathsResynced = true;
                batch.entries.append(entry);
            }
            for (const SdfPath& path : notice.GetResyncedPaths()) {
                NoticeEntry entry;
                entry.path = path;
                if (path.IsPrimPath())
                    entry.primResyncType = notice.GetPrimResyncType(path, &entry.associatedPath);
                batch.entries.append(entry);
            }
            if (batch.entries.isEmpty())
                return;

            bool queueDispatch = false;
            {
                QMutexLocker locker(&d.pendingMutex);
                d.pending.entries.append(batch.entries);
                if (!d.dispatchQueued) {
                    d.dispatchQueued = true;
                    queueDispatch = true;
                }
            }

            if (!queueDispatch || !d.parent->d.session)
                return;

            QMetaObject::invokeMethod(
                d.parent->d.session, [this]() { dispatchPending(); }, Qt::QueuedConnection);
        }

        void blockSignals(bool block) { d.suppress.store(block); }
        bool signalsBlocked() const { return d.suppress.load(); }

        struct Data {
            std::atomic<bool> suppress { false };
            TfNotice::Key key;
            SessionPrivate* parent = nullptr;
            UsdStageRefPtr stage;
            QMutex pendingMutex;
            NoticeBatch pending;
            bool dispatchQueued = false;
        };
        Data d;
    };

    class StageBlocker {
    public:
        explicit StageBlocker(StageWatcher* w)
            : watcher(w)
        {
            if (watcher)
                watcher->blockSignals(true);
        }

        ~StageBlocker()
        {
            if (watcher)
                watcher->blockSignals(false);
        }

    private:
        StageWatcher* watcher = nullptr;
    };

    struct Data {
        UsdStageRefPtr stage;
        UsdStageRefPtr auxiliary;
        Session::LoadPolicy loadPolicy = Session::LoadPolicy::All;
        Session::PrimsUpdate primsUpdate = Session::PrimsUpdate::Immediate;
        Session::StageStatus stageStatus = Session::StageStatus::Closed;
        bool preserveState = true;
        NoticeBatch pendingNotices;
        QString filename;
        QString changeName;
        size_t changeDepth = 0;
        size_t expectedChanges = 0;
        size_t completedChanges = 0;
        std::atomic<bool> changeCancelled { false };
        GfBBox3d bbox;
        QList<SdfPath> mask;
        mutable QReadWriteLock stageLock;
        mutable QReadWriteLock auxiliaryLock;
        QScopedPointer<CommandStack> commandStack;
        QScopedPointer<SelectionList> selectionList;
        QScopedPointer<ViewState> viewState;
        QScopedPointer<StageWatcher> stageWatcher;
        QPointer<Session> session;
    };
    Data d;
};

SessionPrivate::SessionPrivate() { d.stageWatcher.reset(new StageWatcher(this)); }

SessionPrivate::~SessionPrivate() = default;

void
SessionPrivate::init()
{
    qRegisterMetaType<NoticeEntry>("stageviz::NoticeEntry");
    qRegisterMetaType<NoticeBatch>("stageviz::NoticeBatch");
    d.commandStack.reset(new CommandStack());
    d.selectionList.reset(new SelectionList());
    d.viewState.reset(new ViewState());
    d.auxiliary = UsdStage::CreateInMemory("stageviz_auxiliary.usda");
}

void
SessionPrivate::initStage()
{
    UsdStageRefPtr stage;
    const GfBBox3d bbox = boundingBox();
    Session::StageUp up = Session::StageUp::Z;

    {
        READ_LOCKER(locker, &d.stageLock, "stageLock");
        stage = d.stage;
        if (stage) {
            const TfToken upAxis = UsdGeomGetStageUpAxis(stage);
            up = (upAxis == UsdGeomTokens->y) ? Session::StageUp::Y : Session::StageUp::Z;
        }
    }
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        d.stageStatus = Session::StageStatus::Loaded;
        d.bbox = bbox;
        d.pendingNotices.entries.clear();
    }

    if (d.viewState && d.viewState->camera()) {
        ViewCamera* camera = d.viewState->camera();
        camera->setBoundingBox(bbox);
        camera->setCameraUp(up == Session::StageUp::Y ? ViewCamera::Y : ViewCamera::Z);
        camera->resetView();
    }

    d.stageWatcher->watch(stage);
}

void
SessionPrivate::beginProgressBlock(const QString& name, size_t count)
{
    d.changeCancelled.store(false);
    d.changeName = name;
    d.changeDepth++;
    if (d.changeDepth == 1) {
        d.expectedChanges = count;
        d.completedChanges = 0;
        Q_EMIT d.session->progressBlockChanged(name, Session::ProgressMode::Running);
    }
}

void
SessionPrivate::updateProgressNotify(const Session::Notify& notify, size_t completed)
{
    d.completedChanges = completed;
    Q_EMIT d.session->progressNotifyChanged(notify, completed, d.expectedChanges);
}

void
SessionPrivate::cancelProgressBlock()
{
    d.changeCancelled.store(true);
}

void
SessionPrivate::endProgressBlock()
{
    if (d.changeDepth == 0)
        return;

    d.changeDepth--;
    if (d.changeDepth > 0)
        return;

    const bool cancelled = d.changeCancelled.load();
    d.changeCancelled.store(false);

    Q_EMIT d.session->progressBlockChanged(d.changeName, Session::ProgressMode::Idle);
    d.changeName.clear();

    if (cancelled) {
        d.pendingNotices.entries.clear();
        if (d.stageWatcher)
            d.stageWatcher->takePending();
        return;
    }

    flushPrims();
}

bool
SessionPrivate::isProgressBlockCancelled() const
{
    return d.changeCancelled.load();
}

bool
SessionPrivate::newStage(Session::LoadPolicy policy)
{
    close();
    beginProgressBlock("New stage", 1);

    QList<SdfPath> mask;
    bool created = false;
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        StageBlocker blocker(d.stageWatcher.data());
        d.stageWatcher->init();

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        if (!stage) {
            endProgressBlock();
            return false;
        }
        d.stage = stage;
        UsdGeomSetStageMetersPerUnit(d.stage, UsdGeomLinearUnits::millimeters);
        
        UsdGeomXform root = UsdGeomXform::Define(d.stage, SdfPath("/World"));
        d.stage->SetDefaultPrim(root.GetPrim());
        d.filename.clear();
        d.loadPolicy = policy;
        d.mask.clear();
        d.pendingNotices.entries.clear();
        mask = d.mask;
        created = true;
    }
    d.commandStack->clear();
    d.selectionList->clear();

    if (created)
        initStage();

    endProgressBlock();
    setMask(mask);
    updateStage();
    return true;
}

bool
SessionPrivate::loadFromFile(const QString& filename, Session::LoadPolicy policy)
{
    const QString absFilename = QFileInfo(filename).absoluteFilePath();
    const std::string identifier = QStringToString(absFilename);

    close();

    QList<SdfPath> mask;
    bool loaded = false;
    bool preserveState = false;
    QString stateFilename;

    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        StageBlocker blocker(d.stageWatcher.data());
        d.stageWatcher->init();

        if (SdfLayerHandle existingLayer = SdfLayer::Find(identifier))
            existingLayer->Reload(true);

        if (policy == Session::LoadPolicy::All)
            d.stage = UsdStage::Open(identifier, UsdStage::LoadAll);
        else
            d.stage = UsdStage::Open(identifier, UsdStage::LoadNone);

        d.loadPolicy = policy;
        d.mask.clear();
        d.pendingNotices.entries.clear();

        if (d.stage) {
            d.filename = absFilename;
            loaded = true;
            preserveState = d.preserveState;
            stateFilename = QFileInfo(d.filename + ".session").absoluteFilePath();
        }
        else {
            d.stageStatus = Session::StageStatus::Failed;
            return false;
        }

        mask = d.mask;
    }

    d.commandStack->clear();
    d.selectionList->clear();

    const bool hasStateFile = preserveState && QFileInfo::exists(stateFilename);

    if (loaded)
        initStage();

    if (loaded && hasStateFile) {
        if (!loadState(stateFilename)) {
            close();
            return false;
        }
    }

    setMask(mask);
    updateStage();
    return true;
}

bool
SessionPrivate::mergeFromFile(const QString& filename)
{
    const QString absFilename = QFileInfo(filename).absoluteFilePath();
    if (absFilename.endsWith(".session", Qt::CaseInsensitive)) {
        QFile file(absFilename);
        if (!file.exists())
            return false;
        if (!file.open(QIODevice::ReadOnly))
            return false;

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            return false;

        const QJsonObject root = doc.object();
        const QJsonArray payloads = root.value("loadedPayloads").toArray();

        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return false;

        StageBlocker blocker(d.stageWatcher.data());

        for (const QJsonValue& value : payloads) {
            const QString pathString = value.toString().trimmed();
            if (pathString.isEmpty())
                continue;

            const SdfPath path(qt::QStringToString(pathString));
            if (!path.IsAbsolutePath())
                continue;

            const UsdPrim prim = d.stage->GetPrimAtPath(path);
            if (!prim || !prim.IsValid())
                continue;

            if (!stage::isPayload(d.stage, path))
                continue;

            if (!prim.IsLoaded())
                prim.Load();
        }

        return true;
    }

    UsdStageRefPtr sourceStage;
    try {
        sourceStage = UsdStage::Open(QStringToString(absFilename), UsdStage::LoadAll);
    } catch (const std::exception&) {
        return false;
    }

    if (!sourceStage)
        return false;

    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return false;

        StageBlocker blocker(d.stageWatcher.data());
        const SdfLayerHandle destRoot = d.stage->GetRootLayer();
        const SdfLayerHandle srcRoot = sourceStage->GetRootLayer();

        if (!destRoot || !srcRoot)
            return false;

        const std::string srcIdentifier = srcRoot->GetIdentifier();
        const auto& sublayers = destRoot->GetSubLayerPaths();
        if (std::find(sublayers.begin(), sublayers.end(), srcIdentifier) == sublayers.end()) {
            destRoot->GetSubLayerPaths().push_back(srcIdentifier);
        }
    }
    const QString sessionFilename = QFileInfo(absFilename + ".session").absoluteFilePath();
    QFile sessionFile(sessionFilename);
    if (sessionFile.exists()) {
        mergeFromFile(sessionFilename);
    }
    return true;
}

bool
SessionPrivate::saveToFile(const QString& filename)
{
    QString stageFilename;
    bool preserveState = false;

    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");

        try {
            if (!d.stage)
                return false;

            const SdfLayerHandle rootLayer = d.stage->GetRootLayer();
            if (!rootLayer)
                return false;

            stageFilename = QFileInfo(filename).absoluteFilePath();
            preserveState = d.preserveState;

            // export rewrites the root layer into a fresh file without flattening
            // composition, avoiding stale USDC/crate storage after destructive edits.
            const QString absFilename = QFileInfo(d.filename).absoluteFilePath();
            if (!absFilename.isEmpty() && absFilename == stageFilename && !rootLayer->IsAnonymous()) {
                const QString tempFilename = stageFilename + ".stageviz.tmp";

                QFile::remove(tempFilename);

                if (!rootLayer->Export(QStringToString(tempFilename)))
                    return false;

                if (!QFile::remove(stageFilename)) {
                    QFile::remove(tempFilename);
                    return false;
                }

                if (!QFile::rename(tempFilename, stageFilename)) {
                    QFile::remove(tempFilename);
                    return false;
                }
            }
            else {
                if (!rootLayer->Export(QStringToString(stageFilename)))
                    return false;

                // retarget the live layer to the new file. Block stage notices to
                // avoid unnecessary updates for this identity-only change.
                StageBlocker blocker(d.stageWatcher.data());
                rootLayer->SetIdentifier(QStringToString(stageFilename));
            }

            d.filename = stageFilename;
        } catch (const std::exception&) {
            return false;
        }
    }

    if (preserveState) {
        const QString stateFilename = QFileInfo(stageFilename + ".session").absoluteFilePath();
        if (!saveState(stateFilename))
            return false;
    }

    return true;
}

bool
SessionPrivate::copyToFile(const QString& filename)
{
    QString stageFilename;
    bool preserveState = false;

    {
        READ_LOCKER(locker, &d.stageLock, "stageLock");

        try {
            if (!d.stage)
                return false;

            const SdfLayerHandle rootLayer = d.stage->GetRootLayer();
            if (!rootLayer)
                return false;

            stageFilename = QFileInfo(filename).absoluteFilePath();

            if (!rootLayer->Export(QStringToString(stageFilename)))
                return false;

            preserveState = d.preserveState;
        } catch (const std::exception&) {
            return false;
        }
    }

    if (preserveState) {
        const QString stateFilename = QFileInfo(stageFilename + ".session").absoluteFilePath();
        if (!saveState(stateFilename))
            return false;
    }

    return true;
}

bool
SessionPrivate::flattenPathsToFile(const QList<SdfPath>& paths, const QString& filename)
{
    READ_LOCKER(locker, &d.stageLock, "stageLock");
    if (!d.stage)
        return false;

    UsdStagePopulationMask mask;
    const QList<SdfPath> roots = path::topLevelPaths(paths);
    for (const SdfPath& path : roots)
        mask.Add(path);

    if (mask.GetPaths().empty())
        return false;

    UsdStageRefPtr maskedStage = UsdStage::OpenMasked(d.stage->GetRootLayer(), mask);
    if (!maskedStage)
        return false;

    maskedStage->ExpandPopulationMask();
    return maskedStage->Export(QStringToString(QFileInfo(filename).absoluteFilePath()));
}

bool
SessionPrivate::loadState(const QString& filename)
{
    QFile file(QFileInfo(filename).absoluteFilePath());
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonObject cameraObject = root.value("viewCamera").toObject();
    const QJsonArray payloads = root.value("loadedPayloads").toArray();

    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return false;

        StageBlocker blocker(d.stageWatcher.data());

        for (const QJsonValue& value : payloads) {
            const QString pathString = value.toString().trimmed();
            if (pathString.isEmpty())
                continue;

            const SdfPath path(qt::QStringToString(pathString));
            if (!path.IsAbsolutePath())
                continue;

            const UsdPrim prim = d.stage->GetPrimAtPath(path);
            if (!prim || !prim.IsValid())
                continue;

            if (!stage::isPayload(d.stage, path))
                continue;

            if (!prim.IsLoaded())
                prim.Load();
        }
    }

    const GfBBox3d bbox = boundingBox();
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        d.bbox = bbox;
    }

    if (d.viewState && d.viewState->camera()) {
        ViewCamera* camera = d.viewState->camera();

        camera->setBoundingBox(bbox);

        if (!cameraObject.isEmpty()) {
            if (cameraObject.contains("aspectRatio"))
                camera->setAspectRatio(cameraObject.value("aspectRatio").toDouble(camera->aspectRatio()));

            if (cameraObject.contains("fov"))
                camera->setFov(cameraObject.value("fov").toDouble(camera->fov()));

            if (cameraObject.contains("fovDirection")) {
                const QString value = cameraObject.value("fovDirection").toString();
                if (value == "Horizontal")
                    camera->setFovDirection(ViewCamera::Horizontal);
                else if (value == "Vertical")
                    camera->setFovDirection(ViewCamera::Vertical);
            }

            if (cameraObject.contains("nearClipping"))
                camera->setNearClipping(cameraObject.value("nearClipping").toDouble(camera->nearClipping()));

            if (cameraObject.contains("farClipping"))
                camera->setFarClipping(cameraObject.value("farClipping").toDouble(camera->farClipping()));

            if (cameraObject.contains("fit"))
                camera->setFit(cameraObject.value("fit").toDouble(camera->fit()));

            if (cameraObject.contains("cameraMode")) {
                const QString value = cameraObject.value("cameraMode").toString();
                if (value == "None")
                    camera->setCameraMode(ViewCamera::None);
                else if (value == "Truck")
                    camera->setCameraMode(ViewCamera::Truck);
                else if (value == "Tumble")
                    camera->setCameraMode(ViewCamera::Tumble);
                else if (value == "Zoom")
                    camera->setCameraMode(ViewCamera::Zoom);
                else if (value == "Pick")
                    camera->setCameraMode(ViewCamera::Pick);
            }

            if (cameraObject.contains("cameraUp")) {
                const QString value = cameraObject.value("cameraUp").toString();
                if (value == "X")
                    camera->setCameraUp(ViewCamera::X);
                else if (value == "Y")
                    camera->setCameraUp(ViewCamera::Y);
                else if (value == "Z")
                    camera->setCameraUp(ViewCamera::Z);
            }

            const QJsonArray focusPoint = cameraObject.value("focusPoint").toArray();
            if (focusPoint.size() == 3) {
                camera->setFocusPoint(
                    GfVec3d(focusPoint.at(0).toDouble(), focusPoint.at(1).toDouble(), focusPoint.at(2).toDouble()));
            }

            if (cameraObject.contains("axisYaw"))
                camera->setAxisYaw(cameraObject.value("axisYaw").toDouble(camera->axisYaw()));

            if (cameraObject.contains("axisPitch"))
                camera->setAxisPitch(cameraObject.value("axisPitch").toDouble(camera->axisPitch()));

            if (cameraObject.contains("axisRoll"))
                camera->setAxisRoll(cameraObject.value("axisRoll").toDouble(camera->axisRoll()));

            if (cameraObject.contains("cameraDistance"))
                camera->setCameraDistance(cameraObject.value("cameraDistance").toDouble(camera->cameraDistance()));
        }
    }

    Q_EMIT d.session->boundingBoxChanged(bbox);
    return true;
}

bool
SessionPrivate::saveState(const QString& filename)
{
    QString stageFilename;
    QJsonArray payloads;

    {
        READ_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return false;

        stageFilename = QFileInfo(d.filename).absoluteFilePath();

        std::stack<UsdPrim> stack;
        stack.push(d.stage->GetPseudoRoot());

        while (!stack.empty()) {
            const UsdPrim prim = stack.top();
            stack.pop();

            if (!prim || !prim.IsValid())
                continue;

            if (stage::isPayload(d.stage, prim.GetPath()) && prim.IsLoaded())
                payloads.append(qt::SdfPathToQString(prim.GetPath()));

            for (const UsdPrim& child : prim.GetChildren())
                stack.push(child);
        }
    }

    QJsonObject root;
    root["version"] = 1;
    root["stageFile"] = stageFilename;
    root["loadedPayloads"] = payloads;

    if (d.viewState && d.viewState->camera()) {
        ViewCamera* camera = d.viewState->camera();

        auto cameraUp = [](ViewCamera::CameraUp value) {
            switch (value) {
            case ViewCamera::X: return QStringLiteral("X");
            case ViewCamera::Y: return QStringLiteral("Y");
            case ViewCamera::Z: return QStringLiteral("Z");
            }
            return QStringLiteral("Y");
        };

        auto cameraMode = [](ViewCamera::CameraMode value) {
            switch (value) {
            case ViewCamera::None: return QStringLiteral("None");
            case ViewCamera::Truck: return QStringLiteral("Truck");
            case ViewCamera::Tumble: return QStringLiteral("Tumble");
            case ViewCamera::Zoom: return QStringLiteral("Zoom");
            case ViewCamera::Pick: return QStringLiteral("Pick");
            }
            return QStringLiteral("None");
        };

        auto fovDirection = [](ViewCamera::FovDirection value) {
            switch (value) {
            case ViewCamera::Horizontal: return QStringLiteral("Horizontal");
            case ViewCamera::Vertical: return QStringLiteral("Vertical");
            }
            return QStringLiteral("Vertical");
        };

        const GfVec3d focusPoint = camera->focusPoint();

        QJsonArray focusPointArray;
        focusPointArray.append(focusPoint[0]);
        focusPointArray.append(focusPoint[1]);
        focusPointArray.append(focusPoint[2]);

        QJsonObject cameraObject;
        cameraObject["aspectRatio"] = camera->aspectRatio();
        cameraObject["fov"] = camera->fov();
        cameraObject["fovDirection"] = fovDirection(camera->fovDirection());
        cameraObject["nearClipping"] = camera->nearClipping();
        cameraObject["farClipping"] = camera->farClipping();
        cameraObject["fit"] = camera->fit();
        cameraObject["cameraMode"] = cameraMode(camera->cameraMode());
        cameraObject["cameraUp"] = cameraUp(camera->cameraUp());
        cameraObject["focusPoint"] = focusPointArray;
        cameraObject["axisYaw"] = camera->axisYaw();
        cameraObject["axisPitch"] = camera->axisPitch();
        cameraObject["axisRoll"] = camera->axisRoll();
        cameraObject["cameraDistance"] = camera->cameraDistance();

        root["viewCamera"] = cameraObject;
    }

    QFile file(QFileInfo(filename).absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    const QJsonDocument doc(root);
    return file.write(doc.toJson(QJsonDocument::Indented)) != -1;
}

bool
SessionPrivate::close()
{
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        StageBlocker blocker(d.stageWatcher.data());
        d.stageWatcher->init();
        d.stage = nullptr;
        d.stageStatus = Session::StageStatus::Closed;
        d.pendingNotices.entries.clear();
        d.changeDepth = 0;
        d.expectedChanges = 0;
        d.completedChanges = 0;
        d.changeName.clear();
        d.changeCancelled.store(false);
        d.filename.clear();
    }

    d.commandStack->clear();
    d.selectionList->clear();

    updateStage();
    return true;
}

bool
SessionPrivate::reload()
{
    QString filename;
    Session::LoadPolicy loadPolicy;

    {
        READ_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return false;

        filename = d.filename;
        loadPolicy = d.loadPolicy;
    }

    if (filename.isEmpty())
        return false;

    close();
    return loadFromFile(filename, loadPolicy);
}

bool
SessionPrivate::isLoaded() const
{
    READ_LOCKER(locker, &d.stageLock, "stageLock");
    return d.stage != nullptr;
}

void
SessionPrivate::setMask(const QList<SdfPath>& paths)
{
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        d.mask = paths;
    }
    Q_EMIT d.session->maskChanged(paths);
}

void
SessionPrivate::setPayloads(const QList<SdfPath>& paths, bool loaded)
{
    bool changed = false;

    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return;

        StageBlocker blocker(d.stageWatcher.data());

        for (const SdfPath& path : paths) {
            const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
            UsdPrim prim = d.stage->GetPrimAtPath(primPath);
            if (!prim || !prim.IsValid())
                continue;
            if (!stage::isPayload(d.stage, primPath))
                continue;

            if (loaded) {
                if (!prim.IsLoaded()) {
                    prim.Load();
                    changed = true;
                }
            }
            else {
                if (prim.IsLoaded()) {
                    prim.Unload();
                    changed = true;
                }
            }
        }
    }

    if (!changed)
        return;

    const GfBBox3d bbox = boundingBox();

    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        d.bbox = bbox;
    }

    if (d.viewState && d.viewState->camera())
        d.viewState->camera()->setBoundingBox(bbox);

    Q_EMIT d.session->boundingBoxChanged(bbox);
}

void
SessionPrivate::setPreserveState(bool enabled)
{
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        d.preserveState = enabled;
    }
    Q_EMIT d.session->preserveStateChanged(enabled);
}

Session::StageUp
SessionPrivate::stageUp()
{
    READ_LOCKER(locker, &d.stageLock, "stageLock");
    if (!d.stage)
        return Session::StageUp::Z;

    const TfToken upAxis = UsdGeomGetStageUpAxis(d.stage);
    return (upAxis == UsdGeomTokens->y) ? Session::StageUp::Y : Session::StageUp::Z;
}

void
SessionPrivate::setStageUp(Session::StageUp stageUp)
{
    bool changed = false;
    GfBBox3d bbox;
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        if (!d.stage)
            return;

        const TfToken upAxis = (stageUp == Session::StageUp::Y) ? UsdGeomTokens->y : UsdGeomTokens->z;
        if (UsdGeomGetStageUpAxis(d.stage) == upAxis)
            return;

        UsdGeomSetStageUpAxis(d.stage, upAxis);
        changed = true;
    }

    if (!changed)
        return;

    bbox = boundingBox();
    {
        WRITE_LOCKER(locker, &d.stageLock, "stageLock");
        d.bbox = bbox;
    }
    if (d.viewState && d.viewState->camera()) {
        ViewCamera* camera = d.viewState->camera();
        camera->setBoundingBox(bbox);
        camera->setCameraUp(stageUp == Session::StageUp::Y ? ViewCamera::Y : ViewCamera::Z);
    }
    Q_EMIT d.session->stageUpChanged(stageUp);
    Q_EMIT d.session->boundingBoxChanged(bbox);
}

GfBBox3d
SessionPrivate::boundingBox()
{
    UsdStageRefPtr stage;
    QList<SdfPath> mask;
    {
        READ_LOCKER(locker, &d.stageLock, "stageLock");
        stage = d.stage;
        mask = d.mask;
    }

    Q_ASSERT(stage && "stage is not loaded");
    if (!stage)
        return GfBBox3d();

    if (mask.isEmpty()) {
        UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens(), true);
        return bboxCache.ComputeWorldBound(stage->GetPseudoRoot());
    }
    return stage::boundingBox(stage, mask);
}

bool
SessionPrivate::needsBoundingBoxUpdate(const NoticeBatch& batch) const
{
    for (const NoticeEntry& entry : batch.entries) {
        if (entry.resolvedAssetPathsResynced)
            return true;

        if (entry.primResyncType != UsdNotice::ObjectsChanged::PrimResyncType::Invalid)
            return true;

        if (entry.changedInfoOnly && !entry.path.IsPropertyPath())
            return true;
    }

    return false;
}

void
SessionPrivate::updatePrims(const NoticeBatch& batch)
{
    if (batch.entries.isEmpty())
        return;

    if (d.changeDepth > 0 || d.primsUpdate == Session::PrimsUpdate::Deferred) {
        d.pendingNotices.entries.append(batch.entries);
        return;
    }

    const bool updateBBox = needsBoundingBoxUpdate(batch);

    if (updateBBox) {
        const GfBBox3d bbox = boundingBox();
        {
            WRITE_LOCKER(locker, &d.stageLock, "stageLock");
            d.bbox = bbox;
        }

        Q_EMIT d.session->primsChanged(batch);
        Q_EMIT d.session->boundingBoxChanged(bbox);
    }
    else {
        Q_EMIT d.session->primsChanged(batch);
    }
}

void
SessionPrivate::flushPrims()
{
    if (d.stageWatcher) {
        const NoticeBatch watcherBatch = d.stageWatcher->takePending();
        if (!watcherBatch.entries.isEmpty())
            d.pendingNotices.entries.append(watcherBatch.entries);
    }

    if (d.pendingNotices.entries.isEmpty())
        return;

    const NoticeBatch batch = d.pendingNotices;
    d.pendingNotices.entries.clear();

    const bool updateBBox = needsBoundingBoxUpdate(batch);

    if (updateBBox) {
        const GfBBox3d bbox = boundingBox();
        {
            WRITE_LOCKER(locker, &d.stageLock, "stageLock");
            d.bbox = bbox;
        }

        Q_EMIT d.session->primsChanged(batch);
        Q_EMIT d.session->boundingBoxChanged(bbox);
    }
    else {
        Q_EMIT d.session->primsChanged(batch);
    }
}

void
SessionPrivate::updateStage()
{
    UsdStageRefPtr stage;
    Session::LoadPolicy loadPolicy;
    Session::StageStatus stageStatus;
    GfBBox3d bbox;

    {
        READ_LOCKER(locker, &d.stageLock, "stageLock");
        stage = d.stage;
        loadPolicy = d.loadPolicy;
        stageStatus = d.stageStatus;
        bbox = d.bbox;
    }

    Q_EMIT d.session->stageChanged(stage, loadPolicy, stageStatus);
    Q_EMIT d.session->stageUpChanged(stageUp());
    Q_EMIT d.session->boundingBoxChanged(bbox);
}

Session::Session()
    : p(new SessionPrivate())
{
    p->d.session = this;
    p->init();
}

Session::Session(const QString& filename, Session::LoadPolicy loadPolicy)
    : p(new SessionPrivate())
{
    p->d.session = this;
    p->init();
    loadFromFile(filename, loadPolicy);
}

Session::Session(const Session& other)
    : p(other.p)
{}

Session::~Session() = default;

void
Session::beginProgressBlock(const QString& name, size_t count)
{
    p->beginProgressBlock(name, count);
}

void
Session::updateProgressNotify(const Notify& notify, size_t completed)
{
    p->updateProgressNotify(notify, completed);
}

void
Session::cancelProgressBlock()
{
    p->cancelProgressBlock();
}

void
Session::endProgressBlock()
{
    p->endProgressBlock();
}

bool
Session::isProgressBlockCancelled() const
{
    return p->isProgressBlockCancelled();
}

bool
Session::newStage(Session::LoadPolicy policy)
{
    return p->newStage(policy);
}

bool
Session::loadFromFile(const QString& filename, Session::LoadPolicy loadPolicy)
{
    return p->loadFromFile(filename, loadPolicy);
}

bool
Session::mergeFromFile(const QString& filename)
{
    return p->mergeFromFile(filename);
}

bool
Session::saveToFile(const QString& filename)
{
    return p->saveToFile(filename);
}

bool
Session::copyToFile(const QString& filename)
{
    return p->copyToFile(filename);
}

bool
Session::flattenToFile(const QString& filename)
{
    READ_LOCKER(locker, stageLock(), "stageLock");
    if (!p->d.stage)
        return false;

    try {
        return p->d.stage->Export(QStringToString(QFileInfo(filename).absoluteFilePath()));
    } catch (const std::exception&) {
        return false;
    }
}

bool
Session::flattenPathsToFile(const QList<SdfPath>& paths, const QString& filename)
{
    return p->flattenPathsToFile(paths, filename);
}

bool
Session::loadState(const QString& filename)
{
    return p->loadState(filename);
}

bool
Session::saveState(const QString& filename)
{
    return p->saveState(filename);
}

void
Session::setPreserveState(bool enabled)
{
    p->d.preserveState = enabled;
}

bool
Session::reload()
{
    return p->reload();
}

bool
Session::close()
{
    return p->close();
}

bool
Session::isLoaded() const
{
    return p->isLoaded();
}

Session::StageUp
Session::stageUp()
{
    return p->stageUp();
}

void
Session::setStageUp(Session::StageUp stageUp)
{
    p->setStageUp(stageUp);
}

QList<SdfPath>
Session::mask() const
{
    READ_LOCKER(locker, stageLock(), "stageLock");
    return p->d.mask;
}

void
Session::setMask(const QList<SdfPath>& paths)
{
    p->setMask(paths);
}

void
Session::notifyStatus(Notify::Status status, const QString& message, const QString& details)
{
    Q_EMIT notifyStatusChanged(status, message, details);
}

GfBBox3d
Session::boundingBox()
{
    return p->boundingBox();
}

Session::LoadPolicy
Session::loadPolicy() const
{
    READ_LOCKER(locker, stageLock(), "stageLock");
    return p->d.loadPolicy;
}

QString
Session::filename() const
{
    READ_LOCKER(locker, stageLock(), "stageLock");
    return p->d.filename;
}

UsdStageRefPtr
Session::auxiliary() const
{
    READ_LOCKER(locker, auxiliaryLock(), "auxiliaryLock");
    return p->d.auxiliary;
}

UsdStageRefPtr
Session::auxiliaryUnsafe() const
{
    return p->d.auxiliary;
}

QReadWriteLock*
Session::auxiliaryLock() const
{
    return &p->d.auxiliaryLock;
}

UsdStageRefPtr
Session::stage() const
{
    READ_LOCKER(locker, stageLock(), "stageLock");
    Q_ASSERT(p->d.stage && "stage is not loaded");
    return p->d.stage;
}

UsdStageRefPtr
Session::stageUnsafe() const
{
    return p->d.stage;
}

QReadWriteLock*
Session::stageLock() const
{
    return &p->d.stageLock;
}

CommandStack*
Session::commandStack() const
{
    return p->d.commandStack.data();
}

SelectionList*
Session::selectionList() const
{
    return p->d.selectionList.data();
}

ViewState*
Session::viewState() const
{
    return p->d.viewState.data();
}

Session::PrimsUpdate
Session::primsUpdate() const
{
    READ_LOCKER(locker, stageLock(), "stageLock");
    return p->d.primsUpdate;
}

void
Session::setPrimsUpdate(PrimsUpdate update)
{
    bool flush = false;
    {
        WRITE_LOCKER(locker, stageLock(), "stageLock");
        if (p->d.primsUpdate == update)
            return;

        p->d.primsUpdate = update;
        flush = (update == Session::PrimsUpdate::Immediate);
    }

    if (flush)
        p->flushPrims();
}

void
Session::flushPrimsUpdates()
{
    p->flushPrims();
}

}  // namespace stageviz
