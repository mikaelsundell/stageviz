// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https\://github.com/mikaelsundell/stageviz

#include "viewer.h"
#include "application.h"
#include "commandstack.h"
#include "consoledialog.h"
#include "githubclient.h"
#include "messagedialog.h"
#include "mouseevent.h"
#include "notice.h"
#include "os.h"
#include "outlinerview.h"
#include "progressdialog.h"
#include "propertyview.h"
#include "pythondialog.h"
#include "pythonshelf.h"
#include "qtutils.h"
#include "renderview.h"
#include "selectionlist.h"
#include "session.h"
#include "settings.h"
#include "signalguard.h"
#include "style.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewstate.h"
#include <QActionGroup>
#include <QClipboard>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QImageWriter>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <QSizePolicy>
// generated files
#include "ui_viewer.h"

namespace stageviz {

class ViewerPrivate : public QObject, public SignalGuard {
    Q_OBJECT
public:
    ViewerPrivate();
    void init();
    void initDocks();
    void initRecentFiles();
    void initSettings();
    bool loadFile(const QString& fileName);
    bool mergeFile(const QString& fileName);
    OutlinerView* outlinerView();
    PropertyView* propertyView();
    ProgressDialog* progressDialog();
    PythonDialog* pythonDialog();
    ConsoleDialog* consoleDialog();
    RenderView* renderView();
    void showDialog(QDialog* window);
    bool eventFilter(QObject* object, QEvent* event);
    void enable(bool enable);

public Q_SLOTS:
    void newFile();
    void open();
    void merge();
    void save();
    void saveAs();
    void saveCopy();
    void preserveState();
    void exportAll();
    void exportSelected();
    void exportVisible();
    void exportImage();
    void reload();
    void close();
    void saveSettings();
    void exit();
    void undo();
    void redo();
    void clear();
    void copyImage();
    void selectAll();
    void selectInvert();
    void showSelected();
    void showRecursive();
    void hideSelected();
    void hideRecursive();
    void selectVisibleCapture();
    void selectVisibleSelect();
    void selectVisibleClear();
    void stageUpY();
    void stageUpZ();
    void payloadLoad();
    void payloadUnload();
    void payloadSelectInvert();
    void newXform();
    void deleteSelected();
    void isolate(bool checked);
    void frameAll();
    void frameSelected();
    void resetView();
    void transform(bool checked);
    void cameraLight(bool checked);
    void sceneLights(bool checked);
    void sceneShaders(bool checked);
    void grid(bool checked);
    void renderWireframe();
    void renderShaded();
    void renderClay(bool checked);
    void complexityLow();
    void complexityMedium();
    void complexityHigh();
    void complexityVeryHigh();
    void toggleProgress(bool checked);
    void togglePython(bool checked);
    void toggleConsole(bool checked);
    void openAbout();
    void checkUpdates();
    void openGithubReadme();
    void openGithubIssues();
    void backgroundColor();
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void updateAuxiliary(UsdStageRefPtr auxiliary);
    void updateStageUp(Session::StageUp stageUp);
    void updateSelection(const QList<SdfPath>& paths);
    void updatePreserveState(bool enabled);
    void updateMaterialMode(ViewState::MaterialMode mode);
    void notifyStatusChanged(Session::Notify::Status status, const QString& message, const QString& details);

public:
    void updateDockAction(QAction* action, bool checked);
    void updateModified(bool modified);
    void updateRecentFiles(const QString& filename);
    void updateWindowTitle();
    bool saveFile();
    bool saveChanges();
    void clearChanges();
    bool hasChanges() const;

    struct Data {
        Session::LoadPolicy loadPolicy;
        bool modified;
        int changes;
        QStringList arguments;
        QStringList extensions;
        QStringList recentFiles;
        QScopedPointer<MouseEvent> backgroundColorFilter;
        QScopedPointer<Ui_Viewer> ui;
        QPointer<Viewer> viewer;
        QPointer<OutlinerView> outlinerView;
        QPointer<PropertyView> propertyView;
        QPointer<PythonShelf> pythonShelf;
        QPointer<ProgressDialog> progressDialog;
        QPointer<PythonDialog> pythonDialog;
        QPointer<ConsoleDialog> consoleDialog;
    };
    Data d;
};

ViewerPrivate::ViewerPrivate()
{
    d.loadPolicy = Session::LoadPolicy::All;
    d.modified = false;
    d.changes = 0;
    d.extensions = { "usd", "usda", "usdc", "usdz" };
}

void
ViewerPrivate::init()
{
    d.ui.reset(new Ui_Viewer());
    d.ui->setupUi(d.viewer.data());
    attach(d.ui->displayIsolate);
    initDocks();
    ViewState* viewState = session()->viewState();
    const QColor backgroundColor(
        settings()->value("backgroundColor", style()->color(Style::ColorRole::Render)).toString());
    viewState->setBackgroundColor(backgroundColor);
    viewState->setGridColor(style()->color(Style::ColorRole::Grid));
    d.ui->backgroundColor->setStyleSheet("background-color: " + backgroundColor.name() + ";");
    d.backgroundColorFilter.reset(new MouseEvent);
    d.ui->backgroundColor->installEventFilter(d.backgroundColorFilter.data());
    d.viewer->installEventFilter(this);
    d.ui->fileOpen->setIcon(style()->icon(Style::IconRole::Open));
    d.ui->fileExportAll->setIcon(style()->icon(Style::IconRole::Export));
    d.ui->fileExportImage->setIcon(style()->icon(Style::IconRole::ExportImage));
    d.ui->editUndo->setIcon(style()->icon(Style::IconRole::Undo));
    d.ui->editRedo->setIcon(style()->icon(Style::IconRole::Redo));
    d.ui->displayFrameAll->setIcon(style()->icon(Style::IconRole::FrameAll));
    d.ui->displayRenderWireframe->setIcon(style()->icon(Style::IconRole::Wireframe));
    d.ui->displayRenderShaded->setIcon(style()->icon(Style::IconRole::Shaded));
    d.ui->displayTransform->setIcon(style()->icon(Style::IconRole::Transform));
    d.ui->displayTransform->setCheckable(true);
    d.ui->displayTransform->setChecked(false);
    // connect
    connect(d.ui->policyAll, &QAction::triggered, this, [this]() {
        d.loadPolicy = Session::LoadPolicy::All;
        settings()->setValue("loadType", "all");
    });
    connect(d.ui->policyPayload, &QAction::triggered, this, [this]() {
        d.loadPolicy = Session::LoadPolicy::None;
        settings()->setValue("loadType", "payload");
    });
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->policyAll);
        actions->addAction(d.ui->policyPayload);
    }
    connect(d.ui->fileNew, &QAction::triggered, this, &ViewerPrivate::newFile);
    connect(d.ui->fileOpen, &QAction::triggered, this, &ViewerPrivate::open);
    connect(d.ui->fileMerge, &QAction::triggered, this, &ViewerPrivate::merge);
    connect(d.ui->fileSave, &QAction::triggered, this, &ViewerPrivate::save);
    connect(d.ui->fileSaveAs, &QAction::triggered, this, &ViewerPrivate::saveAs);
    connect(d.ui->fileSaveCopy, &QAction::triggered, this, &ViewerPrivate::saveCopy);
    connect(d.ui->filePreserveState, &QAction::triggered, this, &ViewerPrivate::preserveState);
    connect(d.ui->fileExportAll, &QAction::triggered, this, &ViewerPrivate::exportAll);
    connect(d.ui->fileExportSelected, &QAction::triggered, this, &ViewerPrivate::exportSelected);
    connect(d.ui->fileExportVisible, &QAction::triggered, this, &ViewerPrivate::exportVisible);
    connect(d.ui->fileExportImage, &QAction::triggered, this, &ViewerPrivate::exportImage);
    connect(d.ui->fileReload, &QAction::triggered, this, &ViewerPrivate::reload);
    connect(d.ui->fileClose, &QAction::triggered, this, &ViewerPrivate::close);
    connect(d.ui->fileSaveSettings, &QAction::triggered, this, &ViewerPrivate::saveSettings);
    connect(d.ui->fileExit, &QAction::triggered, this, &ViewerPrivate::exit);
    connect(d.ui->editUndo, &QAction::triggered, this, &ViewerPrivate::undo);
    connect(d.ui->editRedo, &QAction::triggered, this, &ViewerPrivate::redo);
    connect(d.ui->editClear, &QAction::triggered, this, &ViewerPrivate::clear);
    connect(d.ui->editCopyImage, &QAction::triggered, this, &ViewerPrivate::copyImage);
    connect(d.ui->editSelectAll, &QAction::triggered, this, &ViewerPrivate::selectAll);
    connect(d.ui->editSelectInvert, &QAction::triggered, this, &ViewerPrivate::selectInvert);
    connect(d.ui->editSelectVisibleCapture, &QAction::triggered, this, &ViewerPrivate::selectVisibleCapture);
    connect(d.ui->editSelectVisibleClear, &QAction::triggered, this, &ViewerPrivate::selectVisibleClear);
    connect(d.ui->editSelectVisibleSelect, &QAction::triggered, this, &ViewerPrivate::selectVisibleSelect);
    connect(d.ui->editShowSelected, &QAction::triggered, this, &ViewerPrivate::showSelected);
    connect(d.ui->editShowRecursive, &QAction::triggered, this, &ViewerPrivate::showRecursive);
    connect(d.ui->editHideSelected, &QAction::triggered, this, &ViewerPrivate::hideSelected);
    connect(d.ui->editHideRecursive, &QAction::triggered, this, &ViewerPrivate::hideRecursive);
    connect(d.ui->editStageUpY, &QAction::triggered, this, &ViewerPrivate::stageUpY);
    connect(d.ui->editStageUpZ, &QAction::triggered, this, &ViewerPrivate::stageUpZ);
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->editStageUpY);
        actions->addAction(d.ui->editStageUpZ);
    }
    connect(d.ui->editPayloadLoad, &QAction::triggered, this, &ViewerPrivate::payloadLoad);
    connect(d.ui->editPayloadUnload, &QAction::triggered, this, &ViewerPrivate::payloadUnload);
    connect(d.ui->editPayloadInvertSelected, &QAction::triggered, this, &ViewerPrivate::payloadSelectInvert);
    connect(d.ui->editNewXform, &QAction::triggered, this, &ViewerPrivate::newXform);
    connect(d.ui->editDeleteSelected, &QAction::triggered, this, &ViewerPrivate::deleteSelected);
    connect(d.ui->displayIsolate, &QAction::toggled, this, &ViewerPrivate::isolate);
    connect(d.ui->displayCameraLight, &QAction::toggled, this, &ViewerPrivate::cameraLight);
    connect(d.ui->displaySceneLights, &QAction::toggled, this, &ViewerPrivate::sceneLights);
    connect(d.ui->displaySceneShaders, &QAction::toggled, this, &ViewerPrivate::sceneShaders);
    connect(d.ui->displayGrid, &QAction::toggled, this, &ViewerPrivate::grid);
    connect(d.ui->displayRenderShaded, &QAction::triggered, this, &ViewerPrivate::renderShaded);
    connect(d.ui->displayRenderWireframe, &QAction::triggered, this, &ViewerPrivate::renderWireframe);
    connect(d.ui->displayRenderClay, &QAction::toggled, this, &ViewerPrivate::renderClay);
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->displayRenderShaded);
        actions->addAction(d.ui->displayRenderWireframe);
    }
    connect(d.ui->displayComplexityLow, &QAction::triggered, this, &ViewerPrivate::complexityLow);
    connect(d.ui->displayComplexityMedium, &QAction::triggered, this, &ViewerPrivate::complexityMedium);
    connect(d.ui->displayComplexityHigh, &QAction::triggered, this, &ViewerPrivate::complexityHigh);
    connect(d.ui->displayComplexityVeryHigh, &QAction::triggered, this, &ViewerPrivate::complexityVeryHigh);
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->displayComplexityLow);
        actions->addAction(d.ui->displayComplexityMedium);
        actions->addAction(d.ui->displayComplexityHigh);
        actions->addAction(d.ui->displayComplexityVeryHigh);
    }
    connect(d.ui->displayFrameAll, &QAction::triggered, this, &ViewerPrivate::frameAll);
    connect(d.ui->displayFrameSelected, &QAction::triggered, this, &ViewerPrivate::frameSelected);
    connect(d.ui->displayResetView, &QAction::triggered, this, &ViewerPrivate::resetView);
    connect(d.ui->displayTransform, &QAction::toggled, this, &ViewerPrivate::transform);
    connect(d.ui->helpAbout, &QAction::triggered, this, &ViewerPrivate::openAbout);
    connect(d.ui->helpCheckUpdates, &QAction::triggered, this, &ViewerPrivate::checkUpdates);
    connect(d.ui->helpGithubReadme, &QAction::triggered, this, &ViewerPrivate::openGithubReadme);
    connect(d.ui->helpGithubIssues, &QAction::triggered, this, &ViewerPrivate::openGithubIssues);
    {
        d.ui->open->setDefaultAction(d.ui->fileOpen);
        d.ui->exportImage->setDefaultAction(d.ui->fileExportImage);
        d.ui->exportAll->setDefaultAction(d.ui->fileExportAll);
        d.ui->frameAll->setDefaultAction(d.ui->displayFrameAll);
        d.ui->redo->setDefaultAction(d.ui->editRedo);
        d.ui->undo->setDefaultAction(d.ui->editUndo);
        d.ui->wireframe->setDefaultAction(d.ui->displayRenderWireframe);
        d.ui->shaded->setDefaultAction(d.ui->displayRenderShaded);
        d.ui->transform->setDefaultAction(d.ui->displayTransform);
    }
    connect(d.backgroundColorFilter.data(), &MouseEvent::pressed, this, &ViewerPrivate::backgroundColor);
    connect(session(), &Session::maskChanged, this, &ViewerPrivate::updateMask);
    connect(session(), &Session::primsChanged, this, &ViewerPrivate::updatePrims);
    connect(session(), &Session::stageChanged, this, &ViewerPrivate::updateStage);
    connect(session(), &Session::auxiliaryChanged, this, &ViewerPrivate::updateAuxiliary);
    connect(session(), &Session::stageUpChanged, this, &ViewerPrivate::updateStageUp);
    connect(session(), &Session::preserveStateChanged, this, &ViewerPrivate::updatePreserveState);
    connect(session(), &Session::notifyStatusChanged, this, &ViewerPrivate::notifyStatusChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &ViewerPrivate::updateSelection);
    connect(session()->commandStack(), &CommandStack::canUndoChanged, d.ui->editUndo, &QAction::setEnabled);
    connect(session()->commandStack(), &CommandStack::canRedoChanged, d.ui->editRedo, &QAction::setEnabled);
    connect(session()->commandStack(), &CommandStack::canClearChanged, d.ui->editClear, &QAction::setEnabled);
    connect(d.ui->hudSceneStats, &QAction::toggled, viewState, &ViewState::setSceneStatsEnabled);
    connect(d.ui->hudPerformanceStats, &QAction::toggled, viewState, &ViewState::setPerformanceStatsEnabled);
    connect(d.ui->hudCameraAxis, &QAction::toggled, viewState, &ViewState::setCameraAxisEnabled);
    connect(d.ui->viewProgress, &QAction::triggered, this, [this]() {
        if (!d.progressDialog)
            return;

        if (!d.progressDialog->isVisible() || d.progressDialog->isMinimized() || !d.progressDialog->isActiveWindow()) {
            showDialog(d.progressDialog);
        }
        else {
            d.progressDialog->hide();
        }
    });
    connect(d.ui->viewPython, &QAction::triggered, this, [this]() {
        if (!d.pythonDialog)
            return;

        if (!d.pythonDialog->isVisible() || d.pythonDialog->isMinimized() || !d.pythonDialog->isActiveWindow()) {
            showDialog(d.pythonDialog);
        }
        else {
            d.pythonDialog->hide();
        }
    });
    connect(d.ui->viewConsole, &QAction::triggered, this, [this]() {
        if (!d.consoleDialog)
            return;

        if (!d.consoleDialog->isVisible() || d.consoleDialog->isMinimized() || !d.consoleDialog->isActiveWindow()) {
            showDialog(d.consoleDialog);
        }
        else {
            d.consoleDialog->hide();
        }
    });
    connect(viewState, &ViewState::materialModeChanged, this, &ViewerPrivate::updateMaterialMode);
    connect(viewState, &ViewState::backgroundColorChanged, this, [this](const QColor& color) {
        d.ui->backgroundColor->setStyleSheet("background-color: " + color.name() + ";");
    });
    connect(viewState, &ViewState::defaultCameraLightEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->displayCameraLight, enabled); });
    connect(viewState, &ViewState::sceneLightsEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->displaySceneLights, enabled); });
    connect(viewState, &ViewState::sceneMaterialsEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->displaySceneShaders, enabled); });
    connect(viewState, &ViewState::gridEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->displayGrid, enabled); });
    connect(viewState, &ViewState::renderModeChanged, this, [this](ViewState::RenderMode mode) {
        updateDockAction(d.ui->displayRenderShaded, mode == ViewState::Shaded);
        updateDockAction(d.ui->displayRenderWireframe, mode == ViewState::Wireframe);
    });
    connect(viewState, &ViewState::complexityLevelChanged, this, [this](ViewState::ComplexityLevel level) {
        updateDockAction(d.ui->displayComplexityLow, level == ViewState::Low);
        updateDockAction(d.ui->displayComplexityMedium, level == ViewState::Medium);
        updateDockAction(d.ui->displayComplexityHigh, level == ViewState::High);
        updateDockAction(d.ui->displayComplexityVeryHigh, level == ViewState::VeryHigh);
    });
    connect(viewState, &ViewState::sceneStatsEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->hudSceneStats, enabled); });
    connect(viewState, &ViewState::performanceStatsEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->hudPerformanceStats, enabled); });
    connect(viewState, &ViewState::cameraAxisEnabledChanged, this,
            [this](bool enabled) { updateDockAction(d.ui->hudCameraAxis, enabled); });

    updateMaterialMode(viewState->materialMode());
    updateDockAction(d.ui->displayRenderShaded, viewState->renderMode() == ViewState::Shaded);
    updateDockAction(d.ui->displayRenderWireframe, viewState->renderMode() == ViewState::Wireframe);
    updateDockAction(d.ui->displayComplexityLow, viewState->complexityLevel() == ViewState::Low);
    updateDockAction(d.ui->displayComplexityMedium, viewState->complexityLevel() == ViewState::Medium);
    updateDockAction(d.ui->displayComplexityHigh, viewState->complexityLevel() == ViewState::High);
    updateDockAction(d.ui->displayComplexityVeryHigh, viewState->complexityLevel() == ViewState::VeryHigh);
    viewState->setDefaultCameraLightEnabled(d.ui->displayCameraLight->isChecked());
    viewState->setSceneLightsEnabled(d.ui->displaySceneLights->isChecked());
    viewState->setSceneMaterialsEnabled(d.ui->displaySceneShaders->isChecked());
    renderView()->setFocus();
    initSettings();
    updateAuxiliary(session()->auxiliary());
    newFile();
}

void
ViewerPrivate::initDocks()
{
    auto addPanelView = [](QWidget* panel, QWidget* view, const QMargins& margins) {
        auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
        if (!layout) {
            layout = new QVBoxLayout(panel);
            layout->setSpacing(0);
            layout->setContentsMargins(margins);
        }
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        layout->addWidget(view, 1);
    };
    d.outlinerView = new OutlinerView(d.ui->outlinerWidget);
    d.outlinerView->setObjectName("outlinerView");
    d.outlinerView->setAttribute(Qt::WA_DeleteOnClose, false);
    addPanelView(d.ui->outlinerWidget, d.outlinerView, QMargins(4, 4, 0, 4));

    d.propertyView = new PropertyView(d.ui->propertyWidget);
    d.propertyView->setObjectName("propertyView");
    d.propertyView->setAttribute(Qt::WA_DeleteOnClose, false);
    addPanelView(d.ui->propertyWidget, d.propertyView, QMargins(4, 4, 0, 4));

    d.pythonShelf = new PythonShelf(d.ui->pythonShelfWidget);
    d.pythonShelf->setObjectName("pythonShelf");
    d.pythonShelf->setAttribute(Qt::WA_DeleteOnClose, false);
    addPanelView(d.ui->pythonShelfWidget, d.pythonShelf, QMargins(0, 0, 0, 0));

    d.ui->splitter->setChildrenCollapsible(false);
    d.ui->splitter->setCollapsible(0, true);
    d.ui->splitter->setCollapsible(1, false);
    d.ui->splitter->setCollapsible(2, true);
    d.ui->splitter->setStretchFactor(0, 0);
    d.ui->splitter->setStretchFactor(1, 1);
    d.ui->splitter->setStretchFactor(2, 0);

    const int left = 280;
    const int right = 280;
    const int total = d.ui->splitter->width() > 0 ? d.ui->splitter->width() : d.viewer->width();
    const int center = std::max(400, total - left - right);
    d.ui->splitter->setSizes({ left, center, right });

    d.progressDialog = new ProgressDialog(d.viewer.data());
    d.progressDialog->setObjectName("progressDialog");
    d.progressDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    d.progressDialog->setWindowFlags(Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MAC
    d.progressDialog->setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    d.progressDialog->setWindowTitle("Progress");
    d.progressDialog->installEventFilter(this);
    d.progressDialog->hide();

    d.pythonDialog = new PythonDialog(d.viewer.data());
    d.pythonDialog->setObjectName("pythonDialog");
    d.pythonDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    d.pythonDialog->setWindowFlags(Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MAC
    d.pythonDialog->setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    d.pythonDialog->setWindowTitle("Python");
    d.pythonDialog->installEventFilter(this);
    d.pythonDialog->hide();

    d.consoleDialog = new ConsoleDialog(d.viewer.data());
    d.consoleDialog->setWindowFlags(Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MAC
    d.consoleDialog->setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    d.consoleDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    d.consoleDialog->setWindowTitle("Console");
    d.consoleDialog->installEventFilter(this);
    d.consoleDialog->hide();

    connect(d.consoleDialog, &ConsoleDialog::visibilityChanged, this,
            [this](bool visible) { updateDockAction(d.ui->viewConsole, visible); });

    d.ui->outlinerWidget->show();
    d.ui->propertyWidget->show();

    updateDockAction(d.ui->viewProgress, false);
    updateDockAction(d.ui->viewPython, false);
    updateDockAction(d.ui->viewConsole, false);
}

void
ViewerPrivate::initRecentFiles()
{
    QMenu* recentMenu = d.ui->fileRecent;
    if (!recentMenu)
        return;

    recentMenu->clear();
    if (d.recentFiles.isEmpty()) {
        QAction* emptyAction = new QAction("No recent files", recentMenu);
        emptyAction->setEnabled(false);
        recentMenu->addAction(emptyAction);
        return;
    }

    for (const QString& file : d.recentFiles) {
        QString fileName = QFileInfo(file).fileName();
        QAction* action = new QAction(fileName, recentMenu);
        action->setToolTip(file);
        action->setData(file);
        connect(action, &QAction::triggered, this, [this, file]() {
            if (!saveChanges())
                return;
            loadFile(file);
        });
        recentMenu->addAction(action);
    }

    recentMenu->addSeparator();
    QAction* clearAction = new QAction("Clear", recentMenu);
    connect(clearAction, &QAction::triggered, this, [this]() {
        d.recentFiles.clear();
        settings()->setValue("recentFiles", QStringList());
        initRecentFiles();
    });
    recentMenu->addAction(clearAction);
}

void
ViewerPrivate::initSettings()
{
    QString loadType = settings()->value("loadType", "all").toString();
    if (loadType == "all") {
        d.loadPolicy = Session::LoadPolicy::All;
        d.ui->policyAll->setChecked(true);
    }
    else {
        d.loadPolicy = Session::LoadPolicy::None;
        d.ui->policyPayload->setChecked(true);
    }

    const bool preserveState = settings()->value("preserveState", true).toBool();
    d.ui->filePreserveState->setChecked(preserveState);
    session()->setPreserveState(preserveState);

    bool sceneStats = settings()->value("sceneStats", true).toBool();
    d.ui->hudSceneStats->setChecked(sceneStats);
    session()->viewState()->setSceneStatsEnabled(sceneStats);

    bool performanceStats = settings()->value("performanceStats", false).toBool();
    d.ui->hudPerformanceStats->setChecked(performanceStats);
    session()->viewState()->setPerformanceStatsEnabled(performanceStats);

    bool cameraAxis = settings()->value("cameraAxis", true).toBool();
    d.ui->hudCameraAxis->setChecked(cameraAxis);
    session()->viewState()->setCameraAxisEnabled(cameraAxis);

    const bool grid = settings()->value("grid", true).toBool();
    d.ui->displayGrid->setChecked(grid);
    session()->viewState()->setGridEnabled(grid);

    d.recentFiles = settings()->value("recentFiles", QStringList()).toStringList();
    initRecentFiles();

    const QByteArray geometry = settings()->value("viewer/windowGeometry").toByteArray();
    if (!geometry.isEmpty())
        d.viewer->restoreGeometry(geometry);

    if (d.ui->splitter) {
        const QByteArray splitterState = settings()->value("viewer/splitterState").toByteArray();
        if (!splitterState.isEmpty())
            d.ui->splitter->restoreState(splitterState);
    }

    const Qt::WindowStates windowState = Qt::WindowStates(
                                             settings()->value("viewer/windowState", int(Qt::WindowNoState)).toInt())
                                         & ~Qt::WindowMinimized;

    if (windowState != Qt::WindowNoState)
        QTimer::singleShot(0, d.viewer, [viewer = d.viewer, windowState]() {
            if (viewer)
                viewer->setWindowState(windowState);
        });
}

bool
ViewerPrivate::loadFile(const QString& fileName)
{
    QFileInfo fileInfo(fileName);
    if (!d.extensions.contains(fileInfo.suffix().toLower())) {
        session()->notifyStatus(Session::Notify::Status::Error,
                                QString("Unsupported file format: %1").arg(fileInfo.suffix()));
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    if (!session()->loadFromFile(fileName, d.loadPolicy)) {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to load file: %1").arg(fileName));
        return false;
    }

    const double elapsedSec = timer.elapsed() / 1000.0;

    settings()->setValue("openDir", fileInfo.absolutePath());
    session()->notifyStatus(Session::Notify::Status::Success,
                            QString("Loaded %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    updateWindowTitle();
    updateRecentFiles(QFileInfo(fileName).absoluteFilePath());
    clearChanges();
    return true;
}

bool
ViewerPrivate::mergeFile(const QString& fileName)
{
    QFileInfo fileInfo(fileName);
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix != "session" && !d.extensions.contains(suffix)) {
        session()->notifyStatus(Session::Notify::Status::Error,
                                QString("Unsupported file format: %1").arg(fileInfo.suffix()));
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    if (!session()->mergeFromFile(fileName)) {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to merge file: %1").arg(fileName));
        return false;
    }

    const double elapsedSec = timer.elapsed() / 1000.0;

    settings()->setValue("openDir", fileInfo.absolutePath());
    session()->notifyStatus(Session::Notify::Status::Success,
                            QString("Merge %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    clearChanges();
    return true;
}

OutlinerView*
ViewerPrivate::outlinerView()
{
    return d.outlinerView.data();
}

PropertyView*
ViewerPrivate::propertyView()
{
    return d.propertyView.data();
}

ProgressDialog*
ViewerPrivate::progressDialog()
{
    return d.progressDialog.data();
}

PythonDialog*
ViewerPrivate::pythonDialog()
{
    return d.pythonDialog.data();
}

ConsoleDialog*
ViewerPrivate::consoleDialog()
{
    return d.consoleDialog.data();
}

RenderView*
ViewerPrivate::renderView()
{
    return d.ui->renderView;
}

void
ViewerPrivate::showDialog(QDialog* dialog)
{
    if (!dialog)
        return;

    if (dialog->isMinimized())
        dialog->showNormal();
    else if (!dialog->isVisible())
        dialog->show();

    dialog->raise();
    dialog->activateWindow();
}

bool
ViewerPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (object == d.progressDialog) {
        switch (event->type()) {
        case QEvent::Show: updateDockAction(d.ui->viewProgress, true); break;
        case QEvent::Hide: updateDockAction(d.ui->viewProgress, false); break;
        default: break;
        }
    }
    if (object == d.pythonDialog) {
        switch (event->type()) {
        case QEvent::Show: updateDockAction(d.ui->viewPython, true); break;
        case QEvent::Hide: updateDockAction(d.ui->viewPython, false); break;
        default: break;
        }
    }
    if (object == d.consoleDialog) {
        switch (event->type()) {
        case QEvent::Show: updateDockAction(d.ui->viewConsole, true); break;
        case QEvent::Hide: updateDockAction(d.ui->viewConsole, false); break;
        default: break;
        }
    }
    if (object == d.viewer
        && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::ActivationChange)) {
        const Qt::WindowStates state = d.viewer->windowState();
        if (!(state & Qt::WindowMinimized) && d.viewer->isActiveWindow()) {
            QTimer::singleShot(0, d.viewer, [this]() {
                if (d.ui->viewProgress->isChecked() && d.progressDialog) {
                    d.progressDialog->show();
                    d.progressDialog->raise();
                }
                if (d.ui->viewPython->isChecked() && d.pythonDialog) {
                    d.pythonDialog->show();
                    d.pythonDialog->raise();
                }
                if (d.ui->viewConsole->isChecked() && d.consoleDialog) {
                    d.consoleDialog->show();
                    d.consoleDialog->raise();
                }
            });
        }
    }

    return QObject::eventFilter(object, event);
}

void
ViewerPrivate::enable(bool enable)
{
    QList<QAction*> actions = { d.ui->fileReload,
                                d.ui->fileClose,
                                d.ui->fileSave,
                                d.ui->fileSaveAs,
                                d.ui->fileSaveCopy,
                                d.ui->filePreserveState,
                                d.ui->fileExportAll,
                                d.ui->fileExportSelected,
                                d.ui->fileExportVisible,
                                d.ui->fileExportImage,
                                d.ui->editCopyImage,
                                d.ui->editSelectAll,
                                d.ui->editShowSelected,
                                d.ui->editShowRecursive,
                                d.ui->editHideSelected,
                                d.ui->editHideRecursive,
                                d.ui->editSelectVisibleCapture,
                                d.ui->editSelectVisibleClear,
                                d.ui->editSelectVisibleSelect,
                                d.ui->editStageUpY,
                                d.ui->editStageUpZ,
                                d.ui->editPayloadLoad,
                                d.ui->editPayloadUnload,
                                d.ui->editPayloadInvertSelected,
                                d.ui->editNewXform,
                                d.ui->editDeleteSelected,
                                d.ui->displayIsolate,
                                d.ui->displayCameraLight,
                                d.ui->displaySceneLights,
                                d.ui->displaySceneShaders,
                                d.ui->displayGrid,
                                d.ui->displayFrameAll,
                                d.ui->displayFrameSelected,
                                d.ui->displayResetView,
                                d.ui->displayTransform,
                                d.ui->displayRenderShaded,
                                d.ui->displayRenderWireframe,
                                d.ui->displayRenderClay,
                                d.ui->displayComplexityLow,
                                d.ui->displayComplexityMedium,
                                d.ui->displayComplexityHigh,
                                d.ui->displayComplexityVeryHigh };
    for (QAction* action : actions) {
        if (action)
            action->setEnabled(enable);
    }
    d.ui->editShow->setEnabled(enable);
    d.ui->editHide->setEnabled(enable);
    d.ui->backgroundColor->setEnabled(enable);
}

void
ViewerPrivate::newFile()
{
    if (!saveChanges())
        return;

    session()->commandStack()->clear();
    if (!session()->newStage(d.loadPolicy)) {
        session()->notifyStatus(Session::Notify::Status::Error, "Failed to create new stage");
        return;
    }

    updateWindowTitle();
    clearChanges();
    enable(true);
}

void
ViewerPrivate::open()
{
    if (!saveChanges())
        return;

    QString openDir = settings()->value("openDir", QDir::homePath()).toString();
    QStringList filters;
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);

    QString filter = QString("USD Files (%1)").arg(filters.join(' '));
    QString filename = QFileDialog::getOpenFileName(d.viewer.data(), "Open USD File", openDir, filter);
    if (!filename.isEmpty())
        loadFile(filename);
}

void
ViewerPrivate::merge()
{
    if (!saveChanges())
        return;

    QString openDir = settings()->value("openDir", QDir::homePath()).toString();

    QStringList filters;
    filters.reserve(d.extensions.size() + 1);
    filters.append("*.session");
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);
    filters.removeDuplicates();

    const QString filter = QString("USD and Session Files (%1)").arg(filters.join(' '));
    const QString filename = QFileDialog::getOpenFileName(d.viewer.data(), "Merge USD File", openDir, filter);

    if (!filename.isEmpty())
        mergeFile(filename);
}

void
ViewerPrivate::save()
{
    saveFile();
}

void
ViewerPrivate::saveAs()
{
    QString saveDir = settings()->value("saveDir", QDir::homePath()).toString();
    QString currentFile = session()->filename();
    QString defaultName;

    if (!currentFile.isEmpty()) {
        QFileInfo info(currentFile);
        defaultName = info.fileName();
        saveDir = info.absolutePath();
    }
    else {
        defaultName = "Untitled.usd";
    }

    QStringList filters;
    filters.reserve(d.extensions.size());
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);

    const QString filter = QString("USD Files (%1)").arg(filters.join(' '));
    QString filename = QFileDialog::getSaveFileName(d.viewer.data(), "Save USD File As",
                                                    QDir(saveDir).filePath(defaultName), filter);

    if (filename.isEmpty())
        return;

    if (QFileInfo(filename).suffix().isEmpty())
        filename += ".usd";

    QElapsedTimer timer;
    timer.start();

    if (session()->saveToFile(filename)) {
        const double elapsedSec = timer.elapsed() / 1000.0;

        settings()->setValue("saveDir", QFileInfo(filename).absolutePath());
        updateWindowTitle();
        updateRecentFiles(QFileInfo(filename).absoluteFilePath());
        clearChanges();
        session()->notifyStatus(Session::Notify::Status::Success,
                                QString("Saved %1 in %2 seconds").arg(filename, QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to save file: %1").arg(filename));
    }
}

void
ViewerPrivate::saveCopy()
{
    QString copyDir = settings()->value("copyDir", QDir::homePath()).toString();
    QString currentFile = session()->filename();
    QString defaultName;

    if (!currentFile.isEmpty()) {
        QFileInfo info(currentFile);
        defaultName = QString("%1 (Copy).%2").arg(info.completeBaseName(), info.suffix());
        copyDir = info.absolutePath();
    }
    else {
        defaultName = "Untitled.usd";
    }

    QStringList filters;
    filters.reserve(d.extensions.size());
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);

    const QString filter = QString("USD Files (%1)").arg(filters.join(' '));
    QString filename = QFileDialog::getSaveFileName(d.viewer.data(), "Save Copy of USD File",
                                                    QDir(copyDir).filePath(defaultName), filter);

    if (filename.isEmpty())
        return;

    if (QFileInfo(filename).suffix().isEmpty())
        filename += ".usd";

    QElapsedTimer timer;
    timer.start();

    if (session()->copyToFile(filename)) {
        const double elapsedSec = timer.elapsed() / 1000.0;

        settings()->setValue("copyDir", QFileInfo(filename).absolutePath());
        session()->notifyStatus(
            Session::Notify::Status::Success,
            QString("Saved copy %1 in %2 seconds").arg(filename, QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to save copy: %1").arg(filename));
    }
}

void
ViewerPrivate::preserveState()
{
    session()->setPreserveState(d.ui->filePreserveState->isChecked());
}

void
ViewerPrivate::exportAll()
{
    QString exportDir = settings()->value("exportAllDir", QDir::homePath()).toString();
    QString currentFile = session()->filename();
    QString defaultName;

    if (!currentFile.isEmpty()) {
        QFileInfo info(currentFile);
        defaultName = QString("%1 (Export all).%2").arg(info.completeBaseName(), info.suffix());
        exportDir = info.absolutePath();
    }
    else {
        defaultName = "Untitled.usd";
    }

    QStringList filters;
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);

    const QString filter = QString("USD Files (%1)").arg(filters.join(' '));
    QString fileName = QFileDialog::getSaveFileName(d.viewer.data(), "Export All",
                                                    QDir(exportDir).filePath(defaultName), filter);

    if (fileName.isEmpty())
        return;

    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += ".usd";

    QElapsedTimer timer;
    timer.start();

    if (session()->flattenToFile(fileName)) {
        const double elapsedSec = timer.elapsed() / 1000.0;
        settings()->setValue("exportAllDir", QFileInfo(fileName).absolutePath());
        session()->notifyStatus(
            Session::Notify::Status::Success,
            QString("Exported all to %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to export all: %1").arg(fileName));
    }
}

void
ViewerPrivate::exportSelected()
{
    QString exportDir = settings()->value("exportSelectedDir", QDir::homePath()).toString();
    QString currentFile = session()->filename();
    QString defaultName;

    if (!currentFile.isEmpty()) {
        QFileInfo info(currentFile);
        defaultName = QString("%1 (Export selected).%2").arg(info.completeBaseName(), info.suffix());
        exportDir = info.absolutePath();
    }
    else {
        defaultName = "Untitled.usd";
    }

    QStringList filters;
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);

    const QString filter = QString("USD Files (%1)").arg(filters.join(' '));
    QString fileName = QFileDialog::getSaveFileName(d.viewer.data(), "Export Selected",
                                                    QDir(exportDir).filePath(defaultName), filter);

    if (fileName.isEmpty())
        return;

    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += ".usd";

    QElapsedTimer timer;
    timer.start();

    if (session()->flattenPathsToFile(session()->selectionList()->paths(), fileName)) {
        const double elapsedSec = timer.elapsed() / 1000.0;
        settings()->setValue("exportSelectedDir", QFileInfo(fileName).absolutePath());
        session()->notifyStatus(
            Session::Notify::Status::Success,
            QString("Exported selected to %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to export selected: %1").arg(fileName));
    }
}

void
ViewerPrivate::exportVisible()
{
    QString exportDir = settings()->value("exportVisibleDir", QDir::homePath()).toString();
    QString currentFile = session()->filename();
    QString defaultName;

    if (!currentFile.isEmpty()) {
        QFileInfo info(currentFile);
        defaultName = QString("%1 (Export visible).%2").arg(info.completeBaseName(), info.suffix());
        exportDir = info.absolutePath();
    }
    else {
        defaultName = "Untitled.usd";
    }

    QStringList filters;
    for (const QString& ext : d.extensions)
        filters.append("*." + ext);

    const QString filter = QString("USD Files (%1)").arg(filters.join(' '));

    QString fileName = QFileDialog::getSaveFileName(d.viewer.data(), "Export Visible",
                                                    QDir(exportDir).filePath(defaultName), filter);

    if (fileName.isEmpty())
        return;

    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += ".usd";

    QList<SdfPath> visiblePaths;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");

        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        visiblePaths = stage::visiblePaths(stage);
    }

    QElapsedTimer timer;
    timer.start();

    if (session()->flattenPathsToFile(visiblePaths, fileName)) {
        const double elapsedSec = timer.elapsed() / 1000.0;
        settings()->setValue("exportVisibleDir", QFileInfo(fileName).absolutePath());
        session()->notifyStatus(
            Session::Notify::Status::Success,
            QString("Exported visible to %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to export visible: %1").arg(fileName));
    }
}

void
ViewerPrivate::exportImage()
{
    QString exportImageDir = settings()->value("exportImageDir", QDir::homePath()).toString();
    QImage image = renderView()->captureImage();

    QStringList filters;
    QList<QByteArray> formats = QImageWriter::supportedImageFormats();
    QString defaultFormat = "png";
    filters.append("PNG Files (*.png)");
    for (const QByteArray& format : formats) {
        QString ext = QString(format).toLower();
        if (ext != defaultFormat)
            filters.append(QString("%1 Files (*.%2)").arg(ext.toUpper(), ext));
    }
    filters.append("All Files (*)");

    QString filename = QFileDialog::getSaveFileName(d.viewer.data(), "Export Image",
                                                    exportImageDir + "/image." + defaultFormat, filters.join(";;"));

    if (filename.isEmpty())
        return;

    QString extension = QFileInfo(filename).suffix().toLower();
    if (extension.isEmpty()) {
        filename += "." + defaultFormat;
        extension = defaultFormat;
    }

    if (!formats.contains(extension.toUtf8())) {
        qWarning() << "unsupported file format: " << extension;
        filename = QFileInfo(filename).completeBaseName() + ".png";
        extension = defaultFormat;
    }

    if (image.save(filename, extension.toUtf8().constData()))
        settings()->setValue("exportImageDir", QFileInfo(filename).absolutePath());
    else
        qWarning() << "failed to save image: " << filename;
}

void
ViewerPrivate::reload()
{
    if (!session()->isLoaded())
        return;
    if (!saveChanges())
        return;

    QElapsedTimer timer;
    timer.start();

    if (!session()->reload()) {
        session()->notifyStatus(Session::Notify::Status::Error, "Failed to reload stage");
        return;
    }

    const double elapsedSec = timer.elapsed() / 1000.0;
    session()->commandStack()->clear();
    session()->notifyStatus(Session::Notify::Status::Success,
                            QString("Reloaded stage in %1 seconds").arg(QString::number(elapsedSec, 'f', 2)));
    clearChanges();
}

void
ViewerPrivate::close()
{
    if (!session()->isLoaded())
        return;
    if (!saveChanges())
        return;

    session()->commandStack()->clear();
    session()->close();
    updateWindowTitle();
    clearChanges();
    enable(false);
}

void
ViewerPrivate::undo()
{
    session()->commandStack()->undo();
}

void
ViewerPrivate::redo()
{
    session()->commandStack()->redo();
}

void
ViewerPrivate::clear()
{
    session()->commandStack()->clear();
}

void
ViewerPrivate::copyImage()
{
    QImage image = renderView()->captureImage();
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setImage(image);
}

void
ViewerPrivate::selectAll()
{
    session()->commandStack()->run(new Command(stageviz::selectAll()));
}

void
ViewerPrivate::selectInvert()
{
    if (session()->selectionList()->paths().size())
        session()->commandStack()->run(new Command(stageviz::selectInvert()));
}

void
ViewerPrivate::saveSettings()
{
    Qt::WindowStates windowState = d.viewer->windowState();
    windowState &= ~Qt::WindowMinimized;
    settings()->setValue("recentFiles", d.recentFiles);
    settings()->setValue("sceneStats", d.ui->hudSceneStats->isChecked());
    settings()->setValue("performanceStats", d.ui->hudPerformanceStats->isChecked());
    settings()->setValue("cameraAxis", d.ui->hudCameraAxis->isChecked());
    settings()->setValue("grid", d.ui->displayGrid->isChecked());
    settings()->setValue("viewer/windowGeometry", d.viewer->saveGeometry());
    settings()->setValue("viewer/windowState", int(windowState));
    settings()->setValue("viewer/splitterState", d.ui->splitter->saveState());
}

void
ViewerPrivate::exit()
{
    d.viewer->close();
}

void
ViewerPrivate::showSelected()
{
    QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.size())
        session()->commandStack()->run(new Command(showPaths(paths, false)));
}

void
ViewerPrivate::showRecursive()
{
    QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.size())
        session()->commandStack()->run(new Command(showPaths(paths, true)));
}

void
ViewerPrivate::hideSelected()
{
    QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.size())
        session()->commandStack()->run(new Command(hidePaths(paths, false)));
}

void
ViewerPrivate::hideRecursive()
{
    QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.size())
        session()->commandStack()->run(new Command(hidePaths(paths, true)));
}

void
ViewerPrivate::selectVisibleCapture()
{
    renderView()->captureVisible();
}

void
ViewerPrivate::selectVisibleSelect()
{
    QList<SdfPath> paths = renderView()->visibleCapturePaths();
    if (paths.size())
        session()->commandStack()->run(new Command(selectPaths(paths)));
}

void
ViewerPrivate::selectVisibleClear()
{
    renderView()->clearVisibleCapture();
}

void
ViewerPrivate::stageUpY()
{
    session()->commandStack()->run(new Command(stageUp(Session::StageUp::Y)));
}

void
ViewerPrivate::stageUpZ()
{
    session()->commandStack()->run(new Command(stageUp(Session::StageUp::Z)));
}

void
ViewerPrivate::payloadLoad()
{
    const QList<SdfPath> selectedPaths = session()->selectionList()->paths();
    if (selectedPaths.isEmpty())
        return;

    QList<SdfPath> payloadPaths;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;
        payloadPaths = stage::resolvePayloadPaths(stage, selectedPaths);
    }

    if (!payloadPaths.isEmpty())
        session()->commandStack()->run(new Command(loadPayloads(payloadPaths)));
}

void
ViewerPrivate::payloadUnload()
{
    const QList<SdfPath> selectedPaths = session()->selectionList()->paths();
    if (selectedPaths.isEmpty())
        return;

    QList<SdfPath> payloadPaths;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;
        payloadPaths = stage::resolvePayloadPaths(stage, selectedPaths);
    }

    if (!payloadPaths.isEmpty())
        session()->commandStack()->run(new Command(unloadPayloads(payloadPaths)));
}

void
ViewerPrivate::payloadSelectInvert()
{
    if (session()->selectionList()->paths().size())
        session()->commandStack()->run(new Command(selectInvertPayload()));
}

void
ViewerPrivate::newXform()
{
    SdfPath parentPath = SdfPath::AbsoluteRootPath();
    const QList<SdfPath> paths = session()->selectionList()->paths();

    if (paths.size() == 1) {
        parentPath = paths.first();
    }
    else if (paths.size() > 1) {
        parentPath = paths.first().GetParentPath();
    }

    if (!parentPath.IsEmpty())
        session()->commandStack()->run(new Command(newXformPath(parentPath, "Xform")));
}

void
ViewerPrivate::deleteSelected()
{
    session()->commandStack()->run(new Command(deletePaths(session()->selectionList()->paths())));
}

void
ViewerPrivate::isolate(bool checked)
{
    const QList<SdfPath> paths = (checked && !session()->selectionList()->paths().isEmpty())
                                     ? session()->selectionList()->paths()
                                     : QList<SdfPath>();
    session()->commandStack()->run(new Command(isolatePaths(paths)));
}

void
ViewerPrivate::frameAll()
{
    if (!session()->isLoaded())
        return;

    const GfBBox3d bbox = session()->boundingBox();
    if (bbox.GetRange().IsEmpty())
        return;

    ViewCamera* camera = session()->viewState()->camera();
    if (camera)
        camera->frame(bbox);
}

void
ViewerPrivate::frameSelected()
{
    const QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.isEmpty())
        return;

    GfBBox3d bbox;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        bbox = stageviz::stage::boundingBox(stage, paths);
    }

    if (bbox.GetRange().IsEmpty())
        return;

    ViewCamera* camera = session()->viewState()->camera();
    if (camera)
        camera->frame(bbox);
}

void
ViewerPrivate::resetView()
{
    if (!session()->isLoaded())
        return;

    ViewCamera* camera = session()->viewState()->camera();
    if (camera)
        camera->resetView();
}

void
ViewerPrivate::transform(bool checked)
{
    renderView()->setTransformEnabled(checked);
}

void
ViewerPrivate::cameraLight(bool checked)
{
    session()->viewState()->setDefaultCameraLightEnabled(checked);
}

void
ViewerPrivate::sceneLights(bool checked)
{
    session()->viewState()->setSceneLightsEnabled(checked);
}

void
ViewerPrivate::sceneShaders(bool checked)
{
    session()->viewState()->setSceneMaterialsEnabled(checked);
}

void
ViewerPrivate::grid(bool checked)
{
    session()->viewState()->setGridEnabled(checked);
}

void
ViewerPrivate::renderShaded()
{
    session()->viewState()->setRenderMode(ViewState::Shaded);
}

void
ViewerPrivate::renderWireframe()
{
    session()->viewState()->setRenderMode(ViewState::Wireframe);
}

void
ViewerPrivate::renderClay(bool checked)
{
    ViewState* state = session()->viewState();
    if (checked) {
        state->setMaterialMode(ViewState::Clay);
    }
    else if (state->materialMode() == ViewState::Clay) {
        state->setMaterialMode(ViewState::All);
    }
}

void
ViewerPrivate::complexityLow()
{
    session()->viewState()->setComplexityLevel(ViewState::Low);
}

void
ViewerPrivate::complexityMedium()
{
    session()->viewState()->setComplexityLevel(ViewState::Medium);
}

void
ViewerPrivate::complexityHigh()
{
    session()->viewState()->setComplexityLevel(ViewState::High);
}

void
ViewerPrivate::complexityVeryHigh()
{
    session()->viewState()->setComplexityLevel(ViewState::VeryHigh);
}

void
ViewerPrivate::toggleProgress(bool checked)
{
    if (!d.progressDialog)
        return;

    if (checked) {
        d.progressDialog->show();
        d.progressDialog->raise();
        d.progressDialog->activateWindow();
    }
    else {
        d.progressDialog->hide();
    }
}

void
ViewerPrivate::togglePython(bool checked)
{
    if (!d.pythonDialog)
        return;
    if (checked) {
        d.pythonDialog->show();
        d.pythonDialog->raise();
        d.pythonDialog->activateWindow();
    }
    else {
        d.pythonDialog->hide();
    }
}

void
ViewerPrivate::toggleConsole(bool checked)
{
    if (!d.consoleDialog)
        return;

    if (checked) {
        d.consoleDialog->show();
        d.consoleDialog->raise();
        d.consoleDialog->activateWindow();
    }
    else {
        d.consoleDialog->hide();
    }
}

void
ViewerPrivate::openAbout()
{
    QFile file(":/text/resources/Copyright.txt");
    QString details;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "failed to open copyright resource:" << file.errorString();
    }
    else {
        QTextStream in(&file);
        details = in.readAll();
    }
    MessageDialog::about(d.viewer.data(), QString("%1 %2").arg(PROJECT_NAME).arg(PROJECT_VERSION), PROJECT_COPYRIGHT,
                         details, GITHUB_URL);
}

void
ViewerPrivate::checkUpdates()
{
    auto compareVersion = [](const QString& version, const QString& other) {
        auto parseVersion = [](QString value) {
            value.remove(QRegularExpression(QStringLiteral("^[^0-9]*")));

            QVector<int> parts;
            const QStringList tokens = value.split('.', Qt::SkipEmptyParts);
            for (const QString& token : tokens) {
                bool ok = false;
                const int number = token.toInt(&ok);
                parts.append(ok ? number : 0);
            }
            return parts;
        };

        const QVector<int> a = parseVersion(version);
        const QVector<int> b = parseVersion(other);
        const qsizetype n = std::max(a.size(), b.size());

        for (qsizetype i = 0; i < n; ++i) {
            const int ai = (i < a.size()) ? a[i] : 0;
            const int bi = (i < b.size()) ? b[i] : 0;
            if (ai < bi)
                return -1;
            if (ai > bi)
                return 1;
        }

        return 0;
    };

    auto* github = new GithubClient(this);

    connect(github, &GithubClient::releasesReceived, this,
            [this, github, compareVersion](const QList<Github::Release>& releases) {
                github->deleteLater();

                if (releases.isEmpty()) {
                    qWarning() << "no github releases found";
                    return;
                }

                const Github::Release& latest = releases.first();

                if (compareVersion(PROJECT_VERSION, latest.tag) < 0) {
                    const QString skipTag = settings()->value("skipTag", QString()).toString();

                    if (skipTag == latest.tag)
                        return;

                    if (latest.assets.isEmpty()) {
                        qWarning() << "no github release assets found";
                        return;
                    }

                    const QString title = QString("There is a new version of %1 available").arg(PROJECT_NAME);
                    const QString heading = tr("What's new in version %1").arg(latest.tag);
                    const QString details = latest.notes;

                    const bool download = MessageDialog::update(d.viewer.data(), title, heading, details,
                                                                latest.url.toString());

                    if (download) {
                        const Github::Asset& asset = latest.assets.first();
                        QDesktopServices::openUrl(asset.url);
                    }
                    else {
                        settings()->setValue("skipTag", latest.tag);
                    }

                    return;
                }

                MessageDialog::information(
                    d.viewer.data(), QString("%1 is up to date").arg(PROJECT_NAME),
                    tr("You are running the latest version of %1 %2.").arg(PROJECT_NAME).arg(PROJECT_VERSION));
            });

    connect(github, &GithubClient::errorOccurred, this, [github](const QString& error) {
        qWarning() << "github error:" << error;
        github->deleteLater();
    });

    github->setUrl(GITHUB_URL);
}

void
ViewerPrivate::openGithubReadme()
{
    QDesktopServices::openUrl(QUrl("https://github.com/mikaelsundell/stageviz/blob/master/README.md"));
}

void
ViewerPrivate::openGithubIssues()
{
    QDesktopServices::openUrl(QUrl("https://github.com/mikaelsundell/stageviz/issues"));
}

void
ViewerPrivate::backgroundColor()
{
    ViewState* state = session()->viewState();
    if (!state)
        return;

    QColor color = QColorDialog::getColor(state->backgroundColor(), d.viewer.data(), "Select color");
    if (!color.isValid())
        return;

    state->setBackgroundColor(color);
    settings()->setValue("backgroundColor", color.name());
}

void
ViewerPrivate::updateSelection(const QList<SdfPath>& paths)
{
    const bool hasSelection = !paths.isEmpty();
    d.ui->editSelectInvert->setEnabled(hasSelection);
    d.ui->editPayloadInvertSelected->setEnabled(hasSelection);

    QList<QAction*> staleActions;
    for (QAction* action : d.ui->menuPayloads->actions()) {
        if (action && action->property("variantMenu").toBool())
            staleActions.append(action);
    }
    for (QAction* action : staleActions) {
        if (QMenu* menu = action->menu()) {
            for (QAction* childAction : menu->actions()) {
                childAction->setShortcut(QKeySequence());
            }
            d.ui->menuPayloads->removeAction(action);
            menu->deleteLater();
        }
        else {
            action->setShortcut(QKeySequence());
            d.ui->menuPayloads->removeAction(action);
            action->deleteLater();
        }
    }

    d.ui->editPayloadLoad->setEnabled(false);
    d.ui->editPayloadUnload->setEnabled(false);

    if (!hasSelection)
        return;

    QList<SdfPath> payloadPaths;
    payload::PayloadVariantTargets variantTargets;
    bool canLoadSelected = false;
    bool canUnloadSelected = false;

    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");

        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        payloadPaths = stage::resolvePayloadPaths(stage, paths);
        variantTargets = payload::payloadVariantTargets(stage, paths);

        for (const SdfPath& payloadPath : payloadPaths) {
            const bool loaded = stage::isLoaded(stage, payloadPath);
            if (loaded)
                canUnloadSelected = true;
            else
                canLoadSelected = true;
            if (canLoadSelected && canUnloadSelected)
                break;
        }
    }

    d.ui->editPayloadLoad->setEnabled(canLoadSelected);
    d.ui->editPayloadUnload->setEnabled(canUnloadSelected);

    if (variantTargets.isEmpty())
        return;

    QAction* separator = d.ui->menuPayloads->addSeparator();
    separator->setProperty("variantMenu", true);

    int index = 0;
    for (auto setIt = variantTargets.cbegin(); setIt != variantTargets.cend(); ++setIt) {
        const QString& setName = setIt.key();

        QMenu* setMenu = d.ui->menuPayloads->addMenu(setName);
        setMenu->menuAction()->setProperty("variantMenu", true);

        d.viewer->addAction(setMenu->menuAction());

        for (auto valueIt = setIt.value().cbegin(); valueIt != setIt.value().cend(); ++valueIt) {
            const QString& value = valueIt.key();
            const QList<SdfPath> targets = valueIt.value();

            QAction* action = setMenu->addAction(value);

            if (index < 10) {
                const int key = (index == 9) ? Qt::Key_0 : (Qt::Key_1 + index);
                action->setShortcut(QKeySequence(key));
                action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
                action->setAutoRepeat(false);
                d.viewer->addAction(action);
            }

            QObject::connect(action, &QAction::triggered, d.viewer, [this, targets, setName, value]() {
                if (!targets.isEmpty()) {
                    session()->commandStack()->run(new Command(loadPayloads(targets, setName, value)));
                }
            });
            ++index;
        }
    }
}

void
ViewerPrivate::updatePreserveState(bool enabled)
{
    d.ui->filePreserveState->blockSignals(true);
    d.ui->filePreserveState->setChecked(enabled);
    d.ui->filePreserveState->blockSignals(false);
    settings()->setValue("preserveState", enabled);
}

void
ViewerPrivate::updateMaterialMode(ViewState::MaterialMode mode)
{
    d.ui->displayRenderClay->blockSignals(true);
    d.ui->displayRenderClay->setChecked(mode == ViewState::Clay);
    d.ui->displayRenderClay->blockSignals(false);
}

void
ViewerPrivate::updateMask(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    d.ui->displayIsolate->setChecked(!paths.isEmpty());
}

void
ViewerPrivate::updatePrims(const NoticeBatch& batch)
{
    if (batch.entries.isEmpty())
        return;
    ++d.changes;
    updateModified(true);
}

void
ViewerPrivate::updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(stage);

    d.loadPolicy = policy;
    d.ui->policyAll->setChecked(policy == Session::LoadPolicy::All);
    d.ui->policyPayload->setChecked(policy == Session::LoadPolicy::None);

    settings()->setValue("loadType", policy == Session::LoadPolicy::All ? "all" : "payload");

    if (status == Session::StageStatus::Loaded)
        enable(true);
}

void
ViewerPrivate::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    renderView()->updateAuxiliary(auxiliary);
}

void
ViewerPrivate::updateStageUp(Session::StageUp stageUp)
{
    d.ui->editStageUpY->setChecked(stageUp == Session::StageUp::Y);
    d.ui->editStageUpZ->setChecked(stageUp == Session::StageUp::Z);
}

void
ViewerPrivate::notifyStatusChanged(Session::Notify::Status status, const QString& message, const QString& details)
{
    QStatusBar* bar = d.ui->statusbar;
    if (!bar)
        return;

    const bool hasError = (status == Session::Notify::Status::Error);
    const bool hasWarning = (status == Session::Notify::Status::Warning);

    const char* level = hasError ? "Error" : hasWarning ? "Warning" : "Info";

    QString logText = QString("[%1] %2").arg(QString::fromLatin1(level), message);
    if (!details.isEmpty())
        logText += QString("\n%1").arg(details);

    if (hasError || hasWarning) {
        std::fprintf(stderr, "%s\n", logText.toUtf8().constData());
        std::fflush(stderr);
    }
    else {
        std::fprintf(stdout, "%s\n", logText.toUtf8().constData());
        std::fflush(stdout);
    }

    const QString text = hasError ? QString(" Error: %1").arg(message) : QString(" %1").arg(message);
    const int timeoutMs = hasError ? 8000 : 4000;

    if (hasError) {
        QColor color = style()->color(Style::ColorRole::Error);
        bar->setStyleSheet(QStringLiteral("QStatusBar { background-color: rgba(%1, %2, %3, %4); }"
                                          "QStatusBar::item { border: none; }")
                               .arg(color.red())
                               .arg(color.green())
                               .arg(color.blue())
                               .arg(color.alpha()));
    }
    else {
        bar->setStyleSheet(QString());
    }

    bar->showMessage(text, timeoutMs);

    QTimer::singleShot(timeoutMs, bar, [bar]() {
        bar->setStyleSheet(QString());
        bar->showMessage(" Ready");
    });
}

void
ViewerPrivate::updateDockAction(QAction* action, bool checked)
{
    if (!action)
        return;
    action->blockSignals(true);
    action->setChecked(checked);
    action->blockSignals(false);
}

void
ViewerPrivate::updateModified(bool modified)
{
    if (d.modified == modified)
        return;
    d.modified = modified;
    updateWindowTitle();
}

void
ViewerPrivate::updateRecentFiles(const QString& filename)
{
    d.recentFiles.removeAll(filename);
    d.recentFiles.prepend(filename);
    const int maxRecent = 10;
    while (d.recentFiles.size() > maxRecent)
        d.recentFiles.removeLast();
    settings()->setValue("recentFiles", d.recentFiles);
    initRecentFiles();
}

void
ViewerPrivate::updateWindowTitle()
{
#ifdef QT_DEBUG
    const QString title = QStringLiteral("%1 %2 (%3 %4)")
                              .arg(QStringLiteral(PROJECT_NAME), QStringLiteral(PROJECT_VERSION),
                                   QStringLiteral(PROJECT_CONFIG), QStringLiteral(PROJECT_DATE));
#else
    const QString title = QStringLiteral("%1 %2").arg(QStringLiteral(PROJECT_NAME), QStringLiteral(PROJECT_VERSION));
#endif

    if (!session()->isLoaded()) {
        d.viewer->setWindowTitle(title);
        return;
    }

    const QString filename = session()->filename();
    QString name = filename.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(filename).fileName();
    if (d.modified)
        name.prepend(QLatin1Char('*'));

    d.viewer->setWindowTitle(QStringLiteral("%1: %2").arg(title, name));
}

bool
ViewerPrivate::saveFile()
{
    const QString filename = session()->filename();
    if (filename.isEmpty()) {
        saveAs();
        return !hasChanges();
    }

    QElapsedTimer timer;
    timer.start();

    if (session()->saveToFile(filename)) {
        const double elapsedSec = timer.elapsed() / 1000.0;
        updateWindowTitle();
        clearChanges();
        session()->notifyStatus(Session::Notify::Status::Success,
                                QString("Saved %1 in %2 seconds").arg(filename, QString::number(elapsedSec, 'f', 2)));
        return true;
    }
    session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to save file: %1").arg(filename));
    return false;
}

bool
ViewerPrivate::saveChanges()
{
    if (!hasChanges())
        return true;

    const QString name = session()->filename().isEmpty() ? "Untitled" : QFileInfo(session()->filename()).fileName();

    if (MessageDialog::question(d.viewer.data(), tr("Save changes?"), tr("Save changes to %1?").arg(name))) {
        return saveFile();
    }
    return true;
}

void
ViewerPrivate::clearChanges()
{
    d.changes = 0;
    updateModified(false);
}

bool
ViewerPrivate::hasChanges() const
{
    return d.changes > 0;
}

Viewer::Viewer(QWidget* parent)
    : QMainWindow(parent)
    , p(new ViewerPrivate())
{
    p->d.viewer = this;
    p->init();
}

Viewer::~Viewer() = default;

void
Viewer::setArguments(const QStringList& arguments)
{
    p->d.arguments = arguments;

    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments[i] == "--open" && i + 1 < arguments.size()) {
            QString filename = arguments[i + 1];
            if (!filename.isEmpty()) {
                p->loadFile(filename);
                return;
            }
        }
    }

    if (arguments.size() == 2) {
        QString arg = arguments[1];
        const QString protocolPrefix = "stageviz://";
        if (arg.startsWith(protocolPrefix, Qt::CaseInsensitive)) {
            QString pathEncoded = arg.mid(protocolPrefix.length());
            QString decodedPath = QUrl::fromPercentEncoding(pathEncoded.toUtf8());
#ifdef Q_OS_WIN
            decodedPath = QDir::fromNativeSeparators(decodedPath);
#endif
            p->loadFile(decodedPath);
            return;
        }
        p->loadFile(arg);
    }
}

void
Viewer::closeEvent(QCloseEvent* event)
{
    if (!p->saveChanges()) {
        event->ignore();
        return;
    }

    p->saveSettings();

    if (p->d.progressDialog) {
        p->d.progressDialog->hide();
        p->d.progressDialog->close();
    }

    if (p->d.pythonDialog) {
        p->d.pythonDialog->hide();
        p->d.pythonDialog->close();
    }

    if (p->d.consoleDialog) {
        p->d.consoleDialog->hide();
        p->d.consoleDialog->close();
    }

    event->accept();
}

void
Viewer::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1) {
            QString filename = urls.first().toLocalFile();
            QString extension = QFileInfo(filename).suffix().toLower();
            if (p->d.extensions.contains(extension)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void
Viewer::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() == 1) {
        if (!p->saveChanges()) {
            event->ignore();
            return;
        }
        QString filename = urls.first().toLocalFile();
        p->loadFile(filename);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

}  // namespace stageviz

#include "viewer.moc"
