// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewer.h"
#include "application.h"
#include "commandstack.h"
#include "consolewidget.h"
#include "dockwidget.h"
#include "mouseevent.h"
#include "notice.h"
#include "os.h"
#include "outlinerview.h"
#include "progressview.h"
#include "pythonview.h"
#include "qtutils.h"
#include "renderview.h"
#include "selectionlist.h"
#include "session.h"
#include "settings.h"
#include "signalguard.h"
#include "style.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <QActionGroup>
#include <QClipboard>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QImageWriter>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

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
    DockWidget* createDock(const QString& objectName, const QString& title, QWidget* view, Qt::DockWidgetArea area);
    void updateDockAction(QAction* action, bool checked);
    DockWidget* outlinerDock();
    DockWidget* progressDock();
    DockWidget* pythonDock();
    OutlinerView* outlinerView();
    ProgressView* progressView();
    PythonView* pythonView();
    ConsoleWidget* consoleWidget();
    RenderView* renderView();
    bool eventFilter(QObject* object, QEvent* event);
    void enable(bool enable);

public Q_SLOTS:
    void newFile();
    void open();
    void merge();
    void save();
    void saveAs();
    void saveCopy();
    void reload();
    void close();
    void undo();
    void redo();
    void clear();
    void copyImage();
    void backgroundColor();
    void exportAll();
    void exportSelected();
    void exportImage();
    void saveSettings();
    void exit();
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
    void payloadVariant(int variant);
    void deleteSelected();
    void isolate(bool checked);
    void frameAll();
    void frameSelected();
    void resetView();
    void collapse();
    void expand();
    void cameraLight(bool checked);
    void sceneLights(bool checked);
    void sceneShaders(bool checked);
    void renderShaded();
    void renderWireframe();
    void light();
    void dark();
    void toggleOutliner(bool checked);
    void toggleProgress(bool checked);
    void togglePython(bool checked);
    void toggleConsole(bool checked);
    void openGithubReadme();
    void openGithubIssues();

public Q_SLOTS:
    void boundingBoxChanged(const GfBBox3d& bbox);
    void selectionChanged(const QList<SdfPath>& paths);
    void maskChanged(const QList<SdfPath>& paths);
    void primsChanged(const NoticeBatch& batch);
    void stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void stageUpChanged(Session::StageUp stageUp);
    void notifyStatusChanged(Session::Notify::Status status, const QString& message);

public:
    void updateModified(bool modified);
    void updateRecentFiles(const QString& filename);
    void updateWindowTitle();
    bool saveFile();
    bool saveChanges();
    void clearChanges();
    bool hasChanges() const;

    struct Data {
        Session::LoadPolicy loadPolicy;
        bool init;
        bool modified;
        int changes;
        QStringList arguments;
        QStringList extensions;
        QStringList recentFiles;
        QColor backgroundColor;
        Qt::DockWidgetArea outlinerArea;
        Qt::DockWidgetArea progressArea;
        Qt::DockWidgetArea pythonArea;
        QScopedPointer<MouseEvent> backgroundColorFilter;
        QScopedPointer<Ui_Viewer> ui;
        QPointer<Viewer> viewer;
        QPointer<DockWidget> outlinerDock;
        QPointer<DockWidget> progressDock;
        QPointer<DockWidget> pythonDock;
        QPointer<OutlinerView> outlinerView;
        QPointer<ProgressView> progressView;
        QPointer<PythonView> pythonView;
        QPointer<ConsoleWidget> consoleWidget;
    };
    Data d;
};

ViewerPrivate::ViewerPrivate()
{
    d.loadPolicy = Session::LoadPolicy::All;
    d.init = false;
    d.modified = false;
    d.changes = 0;
    d.extensions = { "usd", "usda", "usdc", "usdz" };
    d.outlinerArea = Qt::LeftDockWidgetArea;
    d.progressArea = Qt::RightDockWidgetArea;
    d.pythonArea = Qt::RightDockWidgetArea;
}

DockWidget*
ViewerPrivate::createDock(const QString& objectName, const QString& title, QWidget* view, Qt::DockWidgetArea area)
{
    DockWidget* dock = new DockWidget(d.viewer.data());
    dock->setObjectName(objectName);
    dock->setWindowTitle(title);
    dock->setMinimumSize(QSize(350, 0));

    QWidget* contents = new QWidget(dock);
    contents->setObjectName(objectName + "Contents");
    contents->setStyleSheet(QString());
    contents->setProperty("DockWidget", QVariant(true));

    QVBoxLayout* layout = new QVBoxLayout(contents);
    layout->setSpacing(0);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(view);

    dock->setWidget(contents);
    d.viewer->addDockWidget(area, dock);
    return dock;
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
ViewerPrivate::initDocks()
{
    d.outlinerView = new OutlinerView(d.viewer.data());
    d.outlinerView->setObjectName("outlinerView");
    d.outlinerView->setAttribute(Qt::WA_DeleteOnClose, false);
    d.outlinerDock = createDock("outlinerDock", "Outliner", d.outlinerView, d.outlinerArea);
    d.progressView = new ProgressView(d.viewer.data());
    d.progressView->setObjectName("progressView");
    d.progressView->setAttribute(Qt::WA_DeleteOnClose, false);
    d.progressDock = createDock("progressDock", "Progress", d.progressView, d.progressArea);
    d.pythonView = new PythonView(d.viewer.data());
    d.pythonView->setObjectName("pythonView");
    d.pythonView->setAttribute(Qt::WA_DeleteOnClose, false);
    d.pythonDock = createDock("pythonDock", "Python", d.pythonView, d.pythonArea);
    d.consoleWidget = new ConsoleWidget(nullptr);
    d.consoleWidget->setWindowFlags(Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    d.consoleWidget->setObjectName("consoleWidget");
    d.consoleWidget->setAttribute(Qt::WA_DeleteOnClose, false);
    d.consoleWidget->setWindowTitle("Console");
    d.consoleWidget->hide();
    // connect
    connect(d.outlinerDock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea area) { d.outlinerArea = area; });
    connect(d.progressDock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea area) { d.progressArea = area; });
    connect(d.pythonDock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea area) { d.pythonArea = area; });

    connect(d.outlinerDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) { updateDockAction(d.ui->viewOutliner, visible); });
    connect(d.progressDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) { updateDockAction(d.ui->viewProgress, visible); });
    connect(d.pythonDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) { updateDockAction(d.ui->viewPython, visible); });
    connect(d.consoleWidget, &ConsoleWidget::visibilityChanged, this,
            [this](bool visible) { updateDockAction(d.ui->viewConsole, visible); });
    d.outlinerDock->show();
    d.progressDock->show();
    d.pythonDock->hide();
    updateDockAction(d.ui->viewOutliner, true);
    updateDockAction(d.ui->viewProgress, true);
    updateDockAction(d.ui->viewPython, false);
    updateDockAction(d.ui->viewConsole, false);
}

void
ViewerPrivate::init()
{
    os::setDarkTheme();
    d.ui.reset(new Ui_Viewer());
    d.ui->setupUi(d.viewer.data());
    attach(d.ui->displayIsolate);
    initDocks();
    d.backgroundColor = QColor(settings()->value("backgroundColor", style()->color(Style::ColorRole::Render)).toString());
    d.ui->backgroundColor->setStyleSheet("background-color: " + d.backgroundColor.name() + ";");
    d.backgroundColorFilter.reset(new MouseEvent);
    d.ui->backgroundColor->installEventFilter(d.backgroundColorFilter.data());
    d.viewer->installEventFilter(this);
    renderView()->setBackgroundColor(d.backgroundColor);
    d.ui->fileOpen->setIcon(style()->icon(Style::IconRole::Open));
    d.ui->fileExportAll->setIcon(style()->icon(Style::IconRole::Export));
    d.ui->fileExportImage->setIcon(style()->icon(Style::IconRole::ExportImage));
    d.ui->editUndo->setIcon(style()->icon(Style::IconRole::Undo));
    d.ui->editRedo->setIcon(style()->icon(Style::IconRole::Redo));
    d.ui->displayFrameAll->setIcon(style()->icon(Style::IconRole::FrameAll));
    d.ui->displayRenderWireframe->setIcon(style()->icon(Style::IconRole::Wireframe));
    d.ui->displayRenderShaded->setIcon(style()->icon(Style::IconRole::Shaded));
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
    connect(d.ui->fileReload, &QAction::triggered, this, &ViewerPrivate::reload);
    connect(d.ui->fileClose, &QAction::triggered, this, &ViewerPrivate::close);
    connect(d.ui->fileExportAll, &QAction::triggered, this, &ViewerPrivate::exportAll);
    connect(d.ui->fileExportSelected, &QAction::triggered, this, &ViewerPrivate::exportSelected);
    connect(d.ui->fileExportImage, &QAction::triggered, this, &ViewerPrivate::exportImage);
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
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->stageUpY);
        actions->addAction(d.ui->stageUpZ);
    }
    connect(d.ui->editPayloadLoad, &QAction::triggered, this, &ViewerPrivate::payloadLoad);
    connect(d.ui->editPayloadUnload, &QAction::triggered, this, &ViewerPrivate::payloadUnload);
    connect(d.ui->editPayloadInvertSelected, &QAction::triggered, this, &ViewerPrivate::payloadSelectInvert);
    connect(d.ui->editDeleteSelected, &QAction::triggered, this, &ViewerPrivate::deleteSelected);
    connect(d.ui->displayIsolate, &QAction::toggled, this, &ViewerPrivate::isolate);
    connect(d.ui->displayCameraLight, &QAction::toggled, this, &ViewerPrivate::cameraLight);
    connect(d.ui->displaySceneLights, &QAction::toggled, this, &ViewerPrivate::sceneLights);
    connect(d.ui->displaySceneShaders, &QAction::toggled, this, &ViewerPrivate::sceneShaders);
    connect(d.ui->displayRenderShaded, &QAction::triggered, this, &ViewerPrivate::renderShaded);
    connect(d.ui->displayRenderWireframe, &QAction::triggered, this, &ViewerPrivate::renderWireframe);
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->displayRenderShaded);
        actions->addAction(d.ui->displayRenderWireframe);
    }
    connect(d.ui->displayFrameAll, &QAction::triggered, this, &ViewerPrivate::frameAll);
    connect(d.ui->displayFrameSelected, &QAction::triggered, this, &ViewerPrivate::frameSelected);
    connect(d.ui->displayResetView, &QAction::triggered, this, &ViewerPrivate::resetView);
    connect(d.ui->displayCollapse, &QAction::triggered, this, &ViewerPrivate::collapse);
    connect(d.ui->displayExpand, &QAction::triggered, this, &ViewerPrivate::expand);
    connect(d.ui->helpGithubReadme, &QAction::triggered, this, &ViewerPrivate::openGithubReadme);
    connect(d.ui->helpGithubIssues, &QAction::triggered, this, &ViewerPrivate::openGithubIssues);
    {
        d.ui->open->setDefaultAction(d.ui->fileOpen);
        d.ui->exportImage->setDefaultAction(d.ui->fileExportImage);
        d.ui->exportAll->setDefaultAction(d.ui->fileExportAll);
        d.ui->frameAll->setDefaultAction(d.ui->displayFrameAll);
        d.ui->redo->setDefaultAction(d.ui->editRedo);
        d.ui->undo->setDefaultAction(d.ui->editUndo);
        d.ui->wireframe->setDefaultAction(d.ui->displayRenderShaded);
        d.ui->shaded->setDefaultAction(d.ui->displayRenderWireframe);
    }
    connect(d.backgroundColorFilter.data(), &MouseEvent::pressed, this, &ViewerPrivate::backgroundColor);
    connect(d.ui->themeLight, &QAction::triggered, this, &ViewerPrivate::light);
    connect(d.ui->themeDark, &QAction::triggered, this, &ViewerPrivate::dark);
    {
        QActionGroup* actions = new QActionGroup(this);
        actions->setExclusive(true);
        actions->addAction(d.ui->themeLight);
        actions->addAction(d.ui->themeDark);
    }
    connect(session(), &Session::boundingBoxChanged, this, &ViewerPrivate::boundingBoxChanged);
    connect(session(), &Session::maskChanged, this, &ViewerPrivate::maskChanged);
    connect(session(), &Session::primsChanged, this, &ViewerPrivate::primsChanged);
    connect(session(), &Session::stageChanged, this, &ViewerPrivate::stageChanged);
    connect(session(), &Session::stageUpChanged, this, &ViewerPrivate::stageUpChanged);
    connect(session(), &Session::notifyStatusChanged, this, &ViewerPrivate::notifyStatusChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &ViewerPrivate::selectionChanged);
    connect(session()->commandStack(), &CommandStack::canUndoChanged, d.ui->editUndo, &QAction::setEnabled);
    connect(session()->commandStack(), &CommandStack::canRedoChanged, d.ui->editRedo, &QAction::setEnabled);
    connect(session()->commandStack(), &CommandStack::canClearChanged, d.ui->editClear, &QAction::setEnabled);
    connect(d.ui->hudSceneTree, &QAction::toggled, this,
            [=](bool checked) { renderView()->setSceneTreeEnabled(checked); });
    connect(d.ui->hudGpuPerformance, &QAction::toggled, this,
            [=](bool checked) { renderView()->setGpuPerformanceEnabled(checked); });
    connect(d.ui->hudCameraAxis, &QAction::toggled, this,
            [=](bool checked) { renderView()->setCameraAxisEnabled(checked); });
    connect(d.ui->viewOutliner, &QAction::toggled, this, &ViewerPrivate::toggleOutliner);
    connect(d.ui->viewProgress, &QAction::toggled, this, &ViewerPrivate::toggleProgress);
    connect(d.ui->viewPython, &QAction::toggled, this, &ViewerPrivate::togglePython);
    connect(d.ui->viewConsole, &QAction::toggled, this, &ViewerPrivate::toggleConsole);
    renderView()->setFocus();
    initSettings();
    newFile();
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

    bool sceneTree = settings()->value("sceneTree", true).toBool();
    d.ui->hudSceneTree->setChecked(sceneTree);
    renderView()->setSceneTreeEnabled(sceneTree);

    bool gpuPerformance = settings()->value("gpuPerformance", false).toBool();
    d.ui->hudGpuPerformance->setChecked(gpuPerformance);
    renderView()->setGpuPerformanceEnabled(gpuPerformance);

    bool cameraAxis = settings()->value("cameraAxis", true).toBool();
    d.ui->hudCameraAxis->setChecked(cameraAxis);
    renderView()->setCameraAxisEnabled(cameraAxis);

    QString theme = settings()->value("theme", "dark").toString();
    if (theme == "dark") {
        dark();
        d.ui->themeDark->setChecked(true);
    }
    else {
        light();
        d.ui->themeLight->setChecked(true);
    }

    d.recentFiles = settings()->value("recentFiles", QStringList()).toStringList();
    initRecentFiles();
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
    session()->notifyStatus(Session::Notify::Status::Info,
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
    session()->notifyStatus(Session::Notify::Status::Info,
                            QString("Merge %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    clearChanges();
    return true;
}

DockWidget*
ViewerPrivate::outlinerDock()
{
    return d.outlinerDock.data();
}

DockWidget*
ViewerPrivate::progressDock()
{
    return d.progressDock.data();
}

DockWidget*
ViewerPrivate::pythonDock()
{
    return d.pythonDock.data();
}

OutlinerView*
ViewerPrivate::outlinerView()
{
    return d.outlinerView.data();
}

ProgressView*
ViewerPrivate::progressView()
{
    return d.progressView.data();
}

PythonView*
ViewerPrivate::pythonView()
{
    return d.pythonView.data();
}

ConsoleWidget*
ViewerPrivate::consoleWidget()
{
    return d.consoleWidget.data();
}

RenderView*
ViewerPrivate::renderView()
{
    return d.ui->renderView;
}

bool
ViewerPrivate::eventFilter(QObject* object, QEvent* event)
{
    Q_UNUSED(object);

    if (event->type() == QEvent::WindowStateChange) {
        Qt::WindowStates state = d.viewer->windowState();
        if (!(state & Qt::WindowMinimized)) {
            QTimer::singleShot(0, d.viewer, [this]() {
                if (d.ui->viewOutliner->isChecked() && d.outlinerDock && !d.outlinerDock->isVisible())
                    d.outlinerDock->show();
                if (d.ui->viewProgress->isChecked() && d.progressDock && !d.progressDock->isVisible())
                    d.progressDock->show();
                if (d.ui->viewPython->isChecked() && d.pythonDock && !d.pythonDock->isVisible())
                    d.pythonDock->show();
                if (d.ui->viewConsole->isChecked() && d.consoleWidget && !d.consoleWidget->isVisible())
                    d.consoleWidget->show();
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
                                d.ui->fileExportAll,
                                d.ui->fileExportSelected,
                                d.ui->fileExportImage,
                                d.ui->editCopyImage,
                                d.ui->editDeleteSelected,
                                d.ui->editPayloadLoad,
                                d.ui->editPayloadUnload,
                                d.ui->editPayloadInvertSelected,
                                d.ui->editShowSelected,
                                d.ui->editShowRecursive,
                                d.ui->editHideSelected,
                                d.ui->editHideRecursive,
                                d.ui->editSelectVisibleCapture,
                                d.ui->editSelectVisibleClear,
                                d.ui->editSelectVisibleSelect,
                                d.ui->displayIsolate,
                                d.ui->displayFrameAll,
                                d.ui->displayFrameSelected,
                                d.ui->displayResetView,
                                d.ui->displayExpand,
                                d.ui->displayCollapse,
                                d.ui->displayRenderShaded,
                                d.ui->displayRenderWireframe,
                                d.ui->stageUpY,
                                d.ui->stageUpZ };
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

    d.init = false;
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
        session()->notifyStatus(Session::Notify::Status::Info,
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
        session()->notifyStatus(Session::Notify::Status::Info, QString("Saved copy %1 in %2 seconds")
                                                                   .arg(filename, QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to save copy: %1").arg(filename));
    }
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
    session()->notifyStatus(Session::Notify::Status::Info,
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
    d.init = false;
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
ViewerPrivate::backgroundColor()
{
    QColor color = QColorDialog::getColor(d.backgroundColor, d.viewer.data(), "Select color");
    if (!color.isValid())
        return;

    renderView()->setBackgroundColor(color);
    d.ui->backgroundColor->setStyleSheet("background-color: " + color.name() + ";");
    settings()->setValue("backgroundColor", color.name());
    d.backgroundColor = color;
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
            Session::Notify::Status::Info,
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
            Session::Notify::Status::Info,
            QString("Exported selected to %1 in %2 seconds").arg(fileName).arg(QString::number(elapsedSec, 'f', 2)));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Error, QString("Failed to export selected: %1").arg(fileName));
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
ViewerPrivate::saveSettings()
{
    settings()->setValue("recentFiles", d.recentFiles);
    settings()->setValue("sceneTree", d.ui->hudSceneTree->isChecked());
    settings()->setValue("gpuPerformance", d.ui->hudGpuPerformance->isChecked());
    settings()->setValue("cameraAxis", d.ui->hudCameraAxis->isChecked());
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
        payloadPaths = stage::selectionPayloadPaths(stage, selectedPaths);
    }

    if (!payloadPaths.isEmpty())
        session()->commandStack()->run(new Command(loadPayloads(payloadPaths)));
}

void
ViewerPrivate::payloadVariant(int variant)
{
    const QList<SdfPath> selectedPaths = session()->selectionList()->paths();
    if (selectedPaths.isEmpty() || variant < 0)
        return;

    QList<SdfPath> payloadPaths;
    QMap<QString, QList<QString>> variantSets;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        payloadPaths = stage::selectionPayloadPaths(stage, selectedPaths);
        variantSets = stage::findVariantSets(stage, payloadPaths, true);
    }

    if (payloadPaths.isEmpty() || variantSets.isEmpty())
        return;

    int index = 0;
    for (auto it = variantSets.cbegin(); it != variantSets.cend(); ++it) {
        const QString& setName = it.key();
        const QList<QString>& values = it.value();
        for (const QString& value : values) {
            if (index == variant) {
                session()->commandStack()->run(new Command(loadPayloads(payloadPaths, setName, value)));
                return;
            }
            ++index;
        }
    }
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
        payloadPaths = stage::selectionPayloadPaths(stage, selectedPaths);
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
    if (session()->isLoaded())
        renderView()->frameAll();
}

void
ViewerPrivate::frameSelected()
{
    if (session()->selectionList()->paths().size())
        renderView()->frameSelected();
}

void
ViewerPrivate::resetView()
{
    renderView()->resetView();
}

void
ViewerPrivate::collapse()
{
    outlinerView()->collapse();
}

void
ViewerPrivate::expand()
{
    if (session()->selectionList()->paths().size())
        outlinerView()->expand();
}

void
ViewerPrivate::cameraLight(bool checked)
{
    renderView()->setDefaultCameraLightEnabled(checked);
}

void
ViewerPrivate::sceneLights(bool checked)
{
    renderView()->setSceneLightsEnabled(checked);
}

void
ViewerPrivate::sceneShaders(bool checked)
{
    renderView()->setSceneMaterialsEnabled(checked);
}

void
ViewerPrivate::renderShaded()
{
    renderView()->setRenderMode(RenderView::RenderMode::Shaded);
}

void
ViewerPrivate::renderWireframe()
{
    renderView()->setRenderMode(RenderView::RenderMode::Wireframe);
}

void
ViewerPrivate::light()
{
    style()->setTheme(Style::Theme::Light);
    settings()->setValue("theme", "light");
}

void
ViewerPrivate::dark()
{
    style()->setTheme(Style::Theme::Dark);
    settings()->setValue("theme", "dark");
}

void
ViewerPrivate::toggleOutliner(bool checked)
{
    if (!d.outlinerDock)
        return;
    if (checked) {
        d.outlinerDock->show();
        d.outlinerDock->raise();
    }
    else {
        d.outlinerDock->hide();
    }
}

void
ViewerPrivate::toggleProgress(bool checked)
{
    if (!d.progressDock)
        return;
    if (checked) {
        d.progressDock->show();
        d.progressDock->raise();
    }
    else {
        d.progressDock->hide();
    }
}

void
ViewerPrivate::togglePython(bool checked)
{
    if (!d.pythonDock)
        return;
    if (checked) {
        d.pythonDock->show();
        d.pythonDock->raise();
    }
    else {
        d.pythonDock->hide();
    }
}

void
ViewerPrivate::toggleConsole(bool checked)
{
    if (!d.consoleWidget)
        return;

    if (checked) {
        d.consoleWidget->show();
        d.consoleWidget->raise();
        d.consoleWidget->activateWindow();
    }
    else {
        d.consoleWidget->hide();
    }
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
ViewerPrivate::boundingBoxChanged(const GfBBox3d& bbox)
{
    if (!d.init && !bbox.GetRange().IsEmpty()) {
        frameAll();
        d.init = true;
    }
}

void
ViewerPrivate::selectionChanged(const QList<SdfPath>& paths)
{
    const bool hasSelection = !paths.isEmpty();
    d.ui->displayExpand->setEnabled(hasSelection);
    d.ui->editSelectInvert->setEnabled(hasSelection);
    d.ui->editPayloadInvertSelected->setEnabled(hasSelection);

    QList<QMenu*> staleMenus;
    for (QObject* child : d.ui->editPayloadLoad->children()) {
        QMenu* menu = qobject_cast<QMenu*>(child);
        if (menu && menu->property("variantMenu").toBool())
            staleMenus.append(menu);
    }
    for (QMenu* menu : staleMenus)
        delete menu;

    d.ui->editPayloadLoad->setEnabled(false);
    d.ui->editPayloadUnload->setEnabled(false);

    if (!hasSelection)
        return;

    QList<SdfPath> payloadPaths;
    QMap<QString, QList<QString>> variantSets;
    bool canLoadSelected = false;
    bool canUnloadSelected = false;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        payloadPaths = stage::selectionPayloadPaths(stage, paths);
        variantSets = stage::findVariantSets(stage, payloadPaths, true);

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

    if (payloadPaths.isEmpty() || variantSets.isEmpty())
        return;

    d.ui->menuPayloads->addSeparator();

    int index = 0;
    for (auto it = variantSets.cbegin(); it != variantSets.cend(); ++it) {
        const QString& setName = it.key();
        const QList<QString>& values = it.value();

        QMenu* setMenu = d.ui->menuPayloads->addMenu(setName);
        setMenu->setProperty("variantMenu", true);

        for (const QString& value : values) {
            QAction* action = setMenu->addAction(value);
            if (index < 10) {
                const int key = (index == 9) ? Qt::Key_0 : (Qt::Key_1 + index);
                action->setShortcut(QKeySequence(key));
            }

            QObject::connect(action, &QAction::triggered, d.viewer, [this, payloadPaths, setName, value]() {
                if (!payloadPaths.isEmpty())
                    session()->commandStack()->run(new Command(loadPayloads(payloadPaths, setName, value)));
            });

            ++index;
        }
    }
}

void
ViewerPrivate::maskChanged(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    d.ui->displayIsolate->setChecked(!paths.isEmpty());
}

void
ViewerPrivate::primsChanged(const NoticeBatch& batch)
{
    if (batch.entries.isEmpty())
        return;
    ++d.changes;
    updateModified(true);
}

void
ViewerPrivate::stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(stage);
    Q_UNUSED(policy);

    d.init = false;
    if (status == Session::StageStatus::Loaded)
        enable(true);
}

void
ViewerPrivate::stageUpChanged(Session::StageUp stageUp)
{
    d.ui->stageUpY->setChecked(stageUp == Session::StageUp::Y);
    d.ui->stageUpZ->setChecked(stageUp == Session::StageUp::Z);
}

void
ViewerPrivate::notifyStatusChanged(Session::Notify::Status status, const QString& message)
{
    QStatusBar* bar = d.ui->statusbar;
    if (!bar)
        return;

    const bool hasError = (status == Session::Notify::Status::Error);
    const QString text = hasError ? QString(" Error: %1").arg(message) : QString(" %1").arg(message);
    const int timeoutMs = hasError ? 8000 : 4000;

    if (hasError) {
        QColor color = style()->color(Style::ColorRole::Error);
        color.setAlphaF(0.20);
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
        session()->notifyStatus(Session::Notify::Status::Info,
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

    QString name = session()->filename().isEmpty() ? "Untitled" : QFileInfo(session()->filename()).fileName();

    QMessageBox::StandardButton button
        = QMessageBox::warning(d.viewer.data(), PROJECT_NAME, QString("Save changes to %1?").arg(name),
                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    switch (button) {
    case QMessageBox::Save: return saveFile();
    case QMessageBox::Discard: return true;
    case QMessageBox::Cancel:
    default: return false;
    }
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
    if (p->d.consoleWidget) {
        p->d.consoleWidget->hide();
        p->d.consoleWidget->close();
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
