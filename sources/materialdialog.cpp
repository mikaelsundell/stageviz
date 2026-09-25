// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialdialog.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "materialbrowser.h"
#include "materialgraph.h"
#include "materialrenderer.h"
#include "materialtree.h"
#include "materialutils.h"
#include "mime.h"
#include "notice.h"
#include "selectionlist.h"
#include "session.h"
#include "settings.h"
#include "style.h"
#include "tabwidget.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <pxr/base/vt/value.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <vector>

// generated files
#include "ui_materialdialog.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialDialogPrivate : public QObject {
public:
    class GridGraphScene;

    explicit MaterialDialogPrivate(MaterialDialog* dialog);
    ~MaterialDialogPrivate() override;

    void init();
    void refresh();
    void updatePrims(const NoticeBatch& batch);
    void updateSelection();
    void updatePropertySwatch();
    QImage imageNodePreview(const MaterialNodeInfo& node);
    void updateNodeHeader(const SdfPath& path, const QString& name, const QString& type, const QString& shaderId);
    void openMaterialGraph(const SdfPath& materialPath);
    void closeMaterialGraph(int index);
    void refreshGraphs();
    MaterialGraph* currentGraph() const;
    void selectGraphNode(const SdfPath& path);
    void connectGraphSockets(const SdfPath& inputPath, const SdfPath& sourceOutputPath);
    bool ensureSwatchSnapshot();
    void requestSwatch(int row);
    void updateSwatch(const QString& materialPath, const QImage& image);
    void previewFloat(const QString& parameter, double value);
    void previewColor(const QString& parameter, const QColor& value);
    void previewFloatInputs(const QList<SdfPath>& inputPaths, double value);
    void previewColorInputs(const QList<SdfPath>& inputPaths, const QColor& value);
    void editFloat(const QString& parameter, double value);
    void editColor(const QString& parameter, const QColor& value);
    void editFloatInputs(const QList<SdfPath>& inputPaths, double value);
    void editColorInputs(const QList<SdfPath>& inputPaths, const QColor& value);
    bool ensureInputs(const QList<SdfPath>& inputPaths);
    void disconnectInputs(const QList<SdfPath>& inputPaths);
    void resetInputs(const QList<SdfPath>& inputPaths);
    void connectShaderNode(const SdfPath& inputPath, const QString& shaderId, const QString& nodeName,
                           const TfToken& outputName);
    void connectMaterialXNode(const SdfPath& inputPath, const QString& nodeDef, const QString& nodeName);
    void showNewMaterialMenu(QWidget* anchor, const QPoint& globalPosition = QPoint());
    void createPreviewSurface();
    void createStandardSurface();
    void createOpenPBRSurface();
    void loadMaterialX();
    void applyToSelection();
    void selectFromStage();
    void deleteMaterials();
    void renameMaterial(const SdfPath& path, const QString& name);
    void setStatus(const QString& text);
    bool eventFilter(QObject* object, QEvent* event) override;

    static QString oiioColorSpace(const OIIO::ImageSpec& spec);
    static QString automaticTextureColorSpace(const OIIO::ImageSpec& spec);
    static bool isDisplayEncodedColorSpace(const QString& colorSpace);
    static float linearToSrgb(float value);
    static int namedChannel(const OIIO::ImageSpec& spec, const QStringList& names);
    static QImage loadTextureWithOpenImageIO(const QString& filename, QString* error, QString* colorSpaceOut);

    struct Data {
        QHash<QString, QPointer<MaterialGraph>> graphs;
        SdfLayerRefPtr swatchSnapshot;
        bool swatchSnapshotDirty = true;
        bool graphTopologyDirty = true;
        SdfPath previewNode;
        MaterialNodeInfo previewNodeInfo;
        QHash<QString, QImage> texturePreviewCache;
        QStringList texturePreviewCacheOrder;
        QTimer* refreshTimer = nullptr;
        SdfPath pendingMaterialSelection;
        SdfPath pendingRenameSource;
        SdfPath pendingRenameDestination;
        QScopedPointer<Ui_MaterialDialog> ui;
        QScopedPointer<GridGraphScene> graphScene;
        QPointer<TabWidget> tabs;
        QPointer<QGraphicsView> graphView;
        QPointer<MaterialRenderer> renderer;
        QPointer<MaterialDialog> dialog;
        bool initialSplitterSizesApplied = false;
    };

    Data d;
};

class MaterialDialogPrivate::GridGraphScene : public QGraphicsScene {
public:
    using QGraphicsScene::QGraphicsScene;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
};

MaterialDialogPrivate::MaterialDialogPrivate(MaterialDialog* dialog)
{
    d.dialog = dialog;
    init();
}

MaterialDialogPrivate::~MaterialDialogPrivate() = default;

void
MaterialDialogPrivate::GridGraphScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    if (!painter)
        return;

    painter->fillRect(rect, stageviz::style()->color(Style::ColorRole::Graph));

    constexpr qreal spacing = 24.0;
    const qreal left = std::floor(rect.left() / spacing) * spacing;
    const qreal top = std::floor(rect.top() / spacing) * spacing;

    QColor grid = stageviz::style()->color(Style::ColorRole::Grid);
    grid.setAlpha(72);

    QPen gridPen(grid, 1.0);
    gridPen.setCosmetic(true);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(gridPen);

    for (qreal x = left; x <= rect.right(); x += spacing)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (qreal y = top; y <= rect.bottom(); y += spacing)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));

    painter->restore();
}

QString
MaterialDialogPrivate::oiioColorSpace(const OIIO::ImageSpec& spec)
{
    QString value = QString::fromStdString(spec.get_string_attribute("oiio:ColorSpace"));
    if (value.isEmpty())
        value = QString::fromStdString(spec.get_string_attribute("ColorSpace"));
    return value.trimmed();
}

QString
MaterialDialogPrivate::automaticTextureColorSpace(const OIIO::ImageSpec& spec)
{
    const QString tagged = oiioColorSpace(spec);
    if (!tagged.isEmpty())
        return tagged;

    if (spec.format.basetype == OIIO::TypeDesc::HALF || spec.format.basetype == OIIO::TypeDesc::FLOAT
        || spec.format.basetype == OIIO::TypeDesc::DOUBLE) {
        return QStringLiteral("linear");
    }
    return QStringLiteral("sRGB");
}

bool
MaterialDialogPrivate::isDisplayEncodedColorSpace(const QString& colorSpace)
{
    const QString value = colorSpace.toLower();
    return value.contains(QStringLiteral("srgb")) || value.contains(QStringLiteral("s-rgb"))
           || value.contains(QStringLiteral("rec709")) || value.contains(QStringLiteral("rec.709"))
           || value.contains(QStringLiteral("gamma"));
}

float
MaterialDialogPrivate::linearToSrgb(float value)
{
    value = std::max(0.0f, value);
    if (value <= 0.0031308f)
        return value * 12.92f;
    return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

int
MaterialDialogPrivate::namedChannel(const OIIO::ImageSpec& spec, const QStringList& names)
{
    for (int channel = 0; channel < spec.nchannels; ++channel) {
        const QString name = QString::fromStdString(spec.channelnames[channel]).toLower();
        for (const QString& candidate : names) {
            if (name == candidate)
                return channel;
        }
    }
    return -1;
}

QImage
MaterialDialogPrivate::loadTextureWithOpenImageIO(const QString& filename, QString* error, QString* colorSpaceOut)
{
    if (error)
        error->clear();
    if (colorSpaceOut)
        colorSpaceOut->clear();

    QElapsedTimer timer;
    timer.start();

    OIIO::ImageBuf source(filename.toStdString());
    if (!source.read(0, 0, true, OIIO::TypeDesc::FLOAT)) {
        if (error)
            *error = QString::fromStdString(source.geterror());
        return {};
    }

    const OIIO::ImageSpec& spec = source.spec();
    if (spec.width <= 0 || spec.height <= 0 || spec.nchannels <= 0) {
        if (error)
            *error = QStringLiteral("Invalid image dimensions or channel count");
        return {};
    }

    const QString sourceColorSpace = automaticTextureColorSpace(spec);
    if (colorSpaceOut)
        *colorSpaceOut = sourceColorSpace;

    OIIO::ImageBuf converted;
    const OIIO::ImageBuf* display = &source;
    bool colorConverted = false;
    if (!isDisplayEncodedColorSpace(sourceColorSpace)) {
        colorConverted = OIIO::ImageBufAlgo::colorconvert(converted, source, sourceColorSpace.toStdString(), "sRGB");
        if (colorConverted)
            display = &converted;
    }

    const OIIO::ImageSpec& displaySpec = display->spec();
    const int width = displaySpec.width;
    const int height = displaySpec.height;
    const int channels = displaySpec.nchannels;

    std::vector<float> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels));
    const OIIO::ROI roi(displaySpec.x, displaySpec.x + width, displaySpec.y, displaySpec.y + height, displaySpec.z,
                        displaySpec.z + 1, 0, channels);
    if (!display->get_pixels(roi, OIIO::TypeDesc::FLOAT, pixels.data())) {
        if (error)
            *error = QString::fromStdString(display->geterror());
        return {};
    }

    int red = namedChannel(displaySpec, { QStringLiteral("r"), QStringLiteral("red") });
    int green = namedChannel(displaySpec, { QStringLiteral("g"), QStringLiteral("green") });
    int blue = namedChannel(displaySpec, { QStringLiteral("b"), QStringLiteral("blue") });
    const int luminance = namedChannel(displaySpec,
                                       { QStringLiteral("y"), QStringLiteral("l"), QStringLiteral("luminance"),
                                         QStringLiteral("gray"), QStringLiteral("grey") });

    if (channels == 1 || (red < 0 && green < 0 && blue < 0 && luminance >= 0)) {
        const int gray = luminance >= 0 ? luminance : 0;
        red = green = blue = gray;
    }
    else {
        if (red < 0)
            red = 0;
        if (green < 0)
            green = channels > 1 ? 1 : red;
        if (blue < 0)
            blue = channels > 2 ? 2 : red;
    }

    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("Could not allocate RGB preview image");
        return {};
    }

    const bool manualLinearToSrgb = !colorConverted && !isDisplayEncodedColorSpace(sourceColorSpace);
    for (int y = 0; y < height; ++y) {
        uchar* dst = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const size_t base = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x))
                                * static_cast<size_t>(channels);
            float rgb[3] = { pixels[base + static_cast<size_t>(red)], pixels[base + static_cast<size_t>(green)],
                             pixels[base + static_cast<size_t>(blue)] };

            for (float& value : rgb) {
                if (!std::isfinite(value))
                    value = 0.0f;
                if (manualLinearToSrgb)
                    value = linearToSrgb(value);
                value = std::clamp(value, 0.0f, 1.0f);
            }

            dst[x * 3 + 0] = static_cast<uchar>(std::lround(rgb[0] * 255.0f));
            dst[x * 3 + 1] = static_cast<uchar>(std::lround(rgb[1] * 255.0f));
            dst[x * 3 + 2] = static_cast<uchar>(std::lround(rgb[2] * 255.0f));
        }
    }

    qDebug().noquote() << "[MaterialPerf][Dialog] OIIO texture decode" << filename << width << "x" << height << channels
                       << "channels"
                       << "source" << sourceColorSpace
                       << (colorConverted ? "OIIO->sRGB"
                                          : (manualLinearToSrgb ? "linear->sRGB fallback" : "display encoded"))
                       << timer.elapsed() << "ms";
    return image;
}

void
MaterialDialogPrivate::init()
{
    d.ui.reset(new Ui_MaterialDialog());
    d.ui->setupUi(d.dialog.data());
    d.dialog->installEventFilter(this);

    d.renderer = new MaterialRenderer(this);

    d.ui->newMaterial->setIcon(style()->icon(Style::IconRole::New));
    d.ui->load->setIcon(style()->icon(Style::IconRole::Open));
    d.ui->select->setIcon(style()->icon(Style::IconRole::Select));

    d.ui->newMaterial->setText(QString());
    d.ui->load->setText(QString());
    d.ui->select->setText(QString());

    d.ui->newMaterial->setToolTip(tr("New material"));
    d.ui->load->setToolTip(tr("Load MaterialX"));
    d.ui->select->setToolTip(tr("Select materials from selection"));
    d.ui->newMaterial->setContextMenuPolicy(Qt::CustomContextMenu);

    d.ui->splitter->setChildrenCollapsible(true);
    d.ui->splitter->setCollapsible(0, false);
    d.ui->splitter->setCollapsible(1, true);
    d.ui->splitter->setStretchFactor(0, 1);
    d.ui->splitter->setStretchFactor(1, 0);

    d.ui->browserSplitter->setChildrenCollapsible(true);
    d.ui->browserSplitter->setCollapsible(0, false);
    d.ui->browserSplitter->setCollapsible(1, true);
    d.ui->browserSplitter->setStretchFactor(0, 0);
    d.ui->browserSplitter->setStretchFactor(1, 1);

    d.ui->materialSplitter->setChildrenCollapsible(false);
    d.ui->materialSplitter->setCollapsible(0, false);
    d.ui->materialSplitter->setCollapsible(1, false);
    d.ui->materialSplitter->setStretchFactor(0, 0);
    d.ui->materialSplitter->setStretchFactor(1, 1);
    d.ui->materialSplitter->setHandleWidth(1);

    d.tabs = new TabWidget(d.ui->graphWidget);
    d.tabs->setAttribute(Qt::WA_DeleteOnClose, false);
    d.tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d.tabs->setAcceptDrops(true);
    d.tabs->installEventFilter(this);

    d.graphScene.reset(new GridGraphScene(this));
    d.graphView = new QGraphicsView(d.ui->graphWidget);
    d.graphView->setScene(d.graphScene.data());
    d.graphScene->setSceneRect(QRectF(-50000.0, -50000.0, 100000.0, 100000.0));
    d.graphView->setBackgroundBrush(Qt::NoBrush);
    d.graphView->setFrameShape(QFrame::NoFrame);
    d.graphView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d.graphView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d.graphView->setInteractive(false);
    d.graphView->setFocusPolicy(Qt::NoFocus);
    d.graphView->setAcceptDrops(true);
    d.graphView->installEventFilter(this);
    d.graphView->viewport()->setAcceptDrops(true);
    d.graphView->viewport()->installEventFilter(this);

    auto* graphLayout = qobject_cast<QVBoxLayout*>(d.ui->graphWidget->layout());
    graphLayout->addWidget(d.graphView, 1);
    graphLayout->addWidget(d.tabs, 1);

    d.tabs->hide();
    d.graphView->show();

    d.tabs->setTabsClosable(false);
    d.tabs->setMovable(true);
    d.tabs->setUsesScrollButtons(true);
    d.tabs->setElideMode(Qt::ElideNone);

    d.ui->graphWidget->setAcceptDrops(true);
    d.ui->graphWidget->installEventFilter(this);

    if (QTabBar* bar = d.tabs->tabBar()) {
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        bar->setExpanding(false);
        bar->setUsesScrollButtons(true);
        bar->setElideMode(Qt::ElideNone);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bar->setAcceptDrops(true);
        bar->installEventFilter(this);

        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar](const QPoint& pos) {
            const int index = bar->tabAt(pos);
            if (index < 0)
                return;

            QMenu menu(bar);
            QAction* closeAction = menu.addAction(tr("Close"));

            if (menu.exec(bar->mapToGlobal(pos)) == closeAction) {
                closeMaterialGraph(index);
            }
        });
    }

    d.ui->graphWidget->show();
    d.ui->name->setWordWrap(true);
    d.ui->name->setVisible(false);
    d.ui->name->setMinimumHeight(38);
    d.ui->name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QTimer::singleShot(0, d.dialog.data(), [this]() {
        if (!d.dialog || !d.ui)
            return;

        int size = 200;
        {
            const int total = d.ui->splitter->width();
            const int leftWidth = std::max(1, total - size);

            d.ui->splitter->setSizes({ leftWidth, size });
        }
        {
            const int total = d.ui->materialSplitter->height();
            const int treeHeight = std::max(1, total - size);

            d.ui->materialSplitter->setSizes({ size, treeHeight });
        }
    });

    d.refreshTimer = new QTimer(this);
    d.refreshTimer->setSingleShot(true);
    d.refreshTimer->setInterval(160);

    // connect
    connect(d.refreshTimer, &QTimer::timeout, this, [this]() { refresh(); });
    connect(d.ui->browserWidget, &MaterialBrowser::selectionChanged, this, [this]() { updateSelection(); });

    connect(d.ui->browserWidget, &MaterialBrowser::materialActivated, this,
            [this](const SdfPath& path) { openMaterialGraph(path); });
    connect(d.tabs, &TabWidget::currentChanged, this, [this](int) {
        MaterialGraph* graph = currentGraph();
        if (!graph)
            return;

        const int row = d.ui->browserWidget->rowForMaterialPath(graph->materialPath());
        const QList<MaterialEntry> selected = d.ui->browserWidget->selectedEntries();
        if (row >= 0 && (selected.size() != 1 || selected.first().materialPath != graph->materialPath())) {
            d.ui->browserWidget->selectRow(row);
        }
        const SdfPath path = graph->selectedNodePath().IsEmpty() ? graph->material().shaderPath
                                                                 : graph->selectedNodePath();
        if (!path.IsEmpty())
            d.ui->tree->navigateToNode(path);
    });
    connect(d.ui->browserWidget, &MaterialBrowser::swatchRequested, this, [this](int row) { requestSwatch(row); });
    connect(d.ui->browserWidget, &MaterialBrowser::assignRequested, this, [this]() { applyToSelection(); });
    connect(d.ui->browserWidget, &MaterialBrowser::newMaterialRequested, this,
            [this](const QPoint& globalPosition) { showNewMaterialMenu(d.ui->browserWidget, globalPosition); });
    connect(d.ui->browserWidget, &MaterialBrowser::createMaterialRequested, this, [this](const QString& type) {
        if (type == QStringLiteral("UsdPreviewSurface"))
            createPreviewSurface();
        else if (type == QStringLiteral("MaterialXStandardSurface"))
            createStandardSurface();
        else if (type == QStringLiteral("MaterialXOpenPBRSurface"))
            createOpenPBRSurface();
        else if (type == QStringLiteral("MaterialXFile"))
            loadMaterialX();
    });
    connect(d.ui->browserWidget, &MaterialBrowser::deleteRequested, this, [this]() { deleteMaterials(); });
    connect(d.ui->browserWidget, &MaterialBrowser::renameRequested, this,
            [this](const SdfPath& path, const QString& name) { renameMaterial(path, name); });
    connect(d.ui->tree, &MaterialTree::floatPreviewChanged, this,
            [this](const QString& parameter, double value) { previewFloat(parameter, value); });
    connect(d.ui->tree, &MaterialTree::colorPreviewChanged, this,
            [this](const QString& parameter, const QColor& value) { previewColor(parameter, value); });
    connect(d.ui->tree, &MaterialTree::floatChanged, this,
            [this](const QString& parameter, double value) { editFloat(parameter, value); });
    connect(d.ui->tree, &MaterialTree::colorChanged, this,
            [this](const QString& parameter, const QColor& value) { editColor(parameter, value); });
    connect(d.ui->tree, &MaterialTree::floatInputsPreviewChanged, this,
            [this](const QList<SdfPath>& paths, double value) { previewFloatInputs(paths, value); });
    connect(d.ui->tree, &MaterialTree::colorInputsPreviewChanged, this,
            [this](const QList<SdfPath>& paths, const QColor& value) { previewColorInputs(paths, value); });
    connect(d.ui->tree, &MaterialTree::floatInputsChanged, this,
            [this](const QList<SdfPath>& paths, double value) { editFloatInputs(paths, value); });
    connect(d.ui->tree, &MaterialTree::colorInputsChanged, this,
            [this](const QList<SdfPath>& paths, const QColor& value) { editColorInputs(paths, value); });
    connect(d.ui->tree, &MaterialTree::disconnectInputsRequested, this,
            [this](const QList<SdfPath>& paths) { disconnectInputs(paths); });
    connect(d.ui->tree, &MaterialTree::resetInputsRequested, this,
            [this](const QList<SdfPath>& paths) { resetInputs(paths); });
    connect(d.ui->tree, &MaterialTree::connectShaderNodeRequested, this,
            [this](const SdfPath& path, const QString& shaderId, const QString& nodeName, const TfToken& outputName) {
                connectShaderNode(path, shaderId, nodeName, outputName);
            });
    connect(d.ui->tree, &MaterialTree::connectMaterialXNodeRequested, this,
            [this](const SdfPath& path, const QString& nodeDef, const QString& nodeName) {
                connectMaterialXNode(path, nodeDef, nodeName);
            });
    connect(d.ui->tree, &MaterialTree::currentNodeChanged, this,
            [this](const SdfPath& path, const QString& name, const QString& type, const QString& shaderId) {
                d.previewNode = path;
                d.previewNodeInfo = {};

                // Resolve the preview data from the path that actually changed.
                // Do not retain currentNodeInfo() from the previously inspected Image
                // node when switching back to the surface shader.
                {
                    READ_LOCKER(locker, session()->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session()->stageUnsafe();
                    if (stage)
                        d.previewNodeInfo = MaterialUtils::nodeInfo(stage, path);
                }

                updateNodeHeader(path, name, type, shaderId);
                updatePropertySwatch();
            });
    connect(d.renderer, &MaterialRenderer::rendered, this,
            [this](const QString& path, const QImage& image) { updateSwatch(path, image); });

    connect(d.renderer, &MaterialRenderer::error, this,
            [this](const QString&, const QString& message) { setStatus(message); });
    connect(d.ui->newMaterial, &QToolButton::clicked, this, [this]() { showNewMaterialMenu(d.ui->newMaterial); });

    connect(d.ui->newMaterial, &QWidget::customContextMenuRequested, this,
            [this](const QPoint&) { showNewMaterialMenu(d.ui->newMaterial); });
    connect(d.ui->load, &QToolButton::clicked, this, [this]() { loadMaterialX(); });
    connect(d.ui->select, &QToolButton::clicked, this, [this]() { selectFromStage(); });
    connect(session(), &Session::stageChanged, this, [this](UsdStageRefPtr, Session::LoadPolicy, Session::StageStatus) {
        d.swatchSnapshot = nullptr;
        d.swatchSnapshotDirty = true;
        d.graphTopologyDirty = true;
        d.previewNode = {};
        d.previewNodeInfo = {};
        d.texturePreviewCache.clear();
        d.texturePreviewCacheOrder.clear();

        d.renderer->clear();

        while (d.tabs->count() > 0)
            closeMaterialGraph(0);

        d.refreshTimer->start(0);
    });
    connect(session(), &Session::primsChanged, this, [this](const NoticeBatch& batch) {
        if (d.dialog->isVisible())
            updatePrims(batch);
    });
    refresh();
}

bool
MaterialDialogPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (object == d.dialog && event && event->type() == QEvent::Show) {
        refresh();

        if (!d.initialSplitterSizesApplied) {
            d.initialSplitterSizesApplied = true;
            QTimer::singleShot(0, d.dialog.data(), [this]() {
                if (!d.dialog || !d.ui || !d.ui->browserSplitter)
                    return;

                const int total = std::max(2, d.ui->browserSplitter->height());
                const int browserHeight = std::max(1, qRound(static_cast<qreal>(total) * 0.60));
                const int graphHeight = std::max(1, total - browserHeight);
                d.ui->browserSplitter->setSizes({ browserHeight, graphHeight });
            });
        }
    }

    const bool graphDropTarget = object == d.tabs || object == d.ui->graphWidget || object == d.graphView
                                 || (d.graphView && object == d.graphView->viewport())
                                 || (d.tabs && object == d.tabs->tabBar()) || qobject_cast<MaterialGraph*>(object)
                                 || (object && qobject_cast<MaterialGraph*>(object->parent()));

    if (graphDropTarget && event) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* drag = static_cast<QDragMoveEvent*>(event);
            if (drag->mimeData() && drag->mimeData()->hasFormat(mime::material)) {
                drag->setDropAction(Qt::CopyAction);
                drag->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            if (drop->mimeData() && drop->mimeData()->hasFormat(mime::material)) {
                const QString text = QString::fromUtf8(drop->mimeData()->data(mime::material)).trimmed();
                const SdfPath materialPath(text.toStdString());
                if (!materialPath.IsEmpty() && materialPath.IsAbsolutePath() && materialPath.IsPrimPath()) {
                    openMaterialGraph(materialPath);
                    drop->setDropAction(Qt::CopyAction);
                    drop->accept();
                    return true;
                }
            }
        }
    }

    return QObject::eventFilter(object, event);
}

void
MaterialDialogPrivate::setStatus(const QString& text)
{
    d.ui->name->setToolTip(text);
}

void
MaterialDialogPrivate::showNewMaterialMenu(QWidget* anchor, const QPoint& globalPosition)
{
    if (!anchor)
        return;

    QMenu menu(anchor);
    QAction* previewSurface = menu.addAction(tr("USD Preview Surface"));
    QAction* standardSurface = menu.addAction(tr("MaterialX Standard Surface"));
    QAction* openPBRSurface = menu.addAction(tr("MaterialX OpenPBR Surface"));
    menu.addSeparator();
    QAction* materialXFile = menu.addAction(tr("MaterialX File..."));

    const QPoint position = globalPosition.isNull() ? anchor->mapToGlobal(QPoint(0, anchor->height())) : globalPosition;
    QAction* selected = menu.exec(position);

    if (selected == previewSurface)
        createPreviewSurface();
    else if (selected == standardSurface)
        createStandardSurface();
    else if (selected == openPBRSurface)
        createOpenPBRSurface();
    else if (selected == materialXFile)
        loadMaterialX();
}

void
MaterialDialogPrivate::refresh()
{
    QElapsedTimer timer;
    timer.start();
    QList<MaterialEntry> entries;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        entries = MaterialUtils::sceneMaterials(session()->stageUnsafe());
    }

    // Namespace edits change both the material path and every shader path below it.
    // Keep the browser and any already-open graph tab attached to the renamed
    // material instead of leaving the tab indexed by the old USD path.
    const SdfPath renamedSource = d.pendingRenameSource;
    const SdfPath renamedDestination = d.pendingRenameDestination;
    if (!renamedSource.IsEmpty() && !renamedDestination.IsEmpty())
        d.ui->browserWidget->remapEntryPath(renamedSource, renamedDestination);

    d.ui->browserWidget->setEntries(entries);

    if (!renamedSource.IsEmpty() && !renamedDestination.IsEmpty()) {
        const QString oldKey = QString::fromStdString(renamedSource.GetString());
        const QString newKey = QString::fromStdString(renamedDestination.GetString());

        if (MaterialGraph* graph = d.graphs.take(oldKey)) {
            const int row = d.ui->browserWidget->rowForMaterialPath(renamedDestination);
            const MaterialEntry* entry = d.ui->browserWidget->entry(row);
            if (entry) {
                // setMaterial() updates the graph's material/shader paths to the
                // renamed namespace before the regular graph refresh runs.
                graph->setMaterial(*entry);
                d.graphs.insert(newKey, graph);

                const int tabIndex = d.tabs ? d.tabs->indexOf(graph) : -1;
                if (tabIndex >= 0)
                    d.tabs->setTabText(tabIndex, entry->name);
            }
            else {
                // Keep the graph reachable if the composed stage has not caught
                // up yet; a subsequent structural refresh can resolve it.
                d.graphs.insert(oldKey, graph);
            }
        }

        d.pendingRenameSource = SdfPath();
        d.pendingRenameDestination = SdfPath();
    }

    if (d.graphTopologyDirty) {
        refreshGraphs();
        d.graphTopologyDirty = false;
    }

    if (!d.pendingMaterialSelection.IsEmpty()) {
        const int row = d.ui->browserWidget->rowForMaterialPath(d.pendingMaterialSelection);
        if (row >= 0) {
            d.ui->browserWidget->selectRow(row);
            d.pendingMaterialSelection = SdfPath();
            return;
        }
    }

    updateSelection();
    qDebug().noquote() << "[MaterialPerf][Dialog] refresh" << entries.size() << "materials" << timer.elapsed() << "ms";
}

void
MaterialDialogPrivate::updatePrims(const NoticeBatch& batch)
{
    QElapsedTimer timer;
    timer.start();
    if (batch.entries.isEmpty())
        return;

    QSet<QString> dirtyMaterials;
    QList<SdfPath> dirtyProperties;
    bool structuralChange = false;

    auto knownMaterialForPath = [this](SdfPath path) -> SdfPath {
        if (path.IsPropertyPath())
            path = path.GetPrimPath();

        while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath()) {
            if (d.ui->browserWidget->rowForMaterialPath(path) >= 0)
                return path;
            path = path.GetParentPath();
        }
        return {};
    };

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        const SdfPath known = knownMaterialForPath(entry.path);
        const bool resync = entry.resolvedAssetPathsResynced
                            || entry.primResyncType != UsdNotice::ObjectsChanged::PrimResyncType::Invalid;

        if (resync) {
            if (!known.IsEmpty()) {
                structuralChange = true;
                break;
            }

            const SdfPath changedPrim = entry.path.IsPropertyPath() ? entry.path.GetPrimPath() : entry.path;
            for (const MaterialEntry& material : d.ui->browserWidget->entries()) {
                if (material.materialPath.HasPrefix(changedPrim)) {
                    structuralChange = true;
                    break;
                }
            }

            if (structuralChange)
                break;

            READ_LOCKER(locker, session()->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session()->stageUnsafe();
            if (stage) {
                const UsdPrim prim = stage->GetPrimAtPath(changedPrim);
                if (prim) {
                    if (prim.IsA<UsdShadeMaterial>()) {
                        structuralChange = true;
                    }
                    else {
                        for (const UsdPrim& descendant : UsdPrimRange(prim)) {
                            if (descendant.IsA<UsdShadeMaterial>()) {
                                structuralChange = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (structuralChange)
                break;
            continue;
        }

        if (!known.IsEmpty()) {
            dirtyMaterials.insert(QString::fromStdString(known.GetString()));
            if (entry.path.IsPropertyPath())
                dirtyProperties.append(entry.path);
        }
    }

    if (structuralChange) {
        d.swatchSnapshotDirty = true;
        d.graphTopologyDirty = true;
        const QList<MaterialEntry>& entries = d.ui->browserWidget->entries();
        for (int row = 0; row < entries.size(); ++row) {
            d.ui->browserWidget->invalidateSwatch(row);
            d.renderer->invalidate(entries[row].materialPath);
        }
        d.refreshTimer->start();
        qDebug().noquote() << "[MaterialPerf][Dialog] updatePrims structural" << batch.entries.size()
                           << "notice entries"
                           << "invalidate+schedule" << timer.elapsed() << "ms";
        return;
    }

    if (dirtyMaterials.isEmpty())
        return;

    // Ordinary authored value edits stay on the fast path. Mirror their composed
    // values into the persistent preview stages instead of flattening the entire
    // application stage again. Connections/node topology are marked dirty by the
    // explicit structural edit paths below and use a complete snapshot refresh.
    QList<QPair<SdfPath, VtValue>> valueUpdates;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        for (const SdfPath& propertyPath : std::as_const(dirtyProperties)) {
            const UsdAttribute attribute = stage->GetAttributeAtPath(propertyPath);
            if (!attribute)
                continue;
            VtValue value;
            if (attribute.Get(&value) && !value.IsEmpty())
                valueUpdates.append(qMakePair(propertyPath, value));
        }

        const QList<MaterialEntry> refreshed = MaterialUtils::sceneMaterials(stage);
        for (const QString& pathString : std::as_const(dirtyMaterials)) {
            const SdfPath materialPath(pathString.toStdString());
            const int row = d.ui->browserWidget->rowForMaterialPath(materialPath);
            if (row < 0)
                continue;

            auto found = std::find_if(refreshed.cbegin(), refreshed.cend(), [&](const MaterialEntry& candidate) {
                return candidate.materialPath == materialPath;
            });
            if (found == refreshed.cend()) {
                structuralChange = true;
                break;
            }

            d.ui->browserWidget->updateEntry(row, *found);
            d.ui->browserWidget->invalidateSwatch(row);
            d.renderer->invalidate(materialPath);
        }
    }

    if (structuralChange) {
        d.swatchSnapshotDirty = true;
        d.graphTopologyDirty = true;
        d.refreshTimer->start();
        return;
    }

    for (const auto& update : std::as_const(valueUpdates))
        d.renderer->syncAttribute(update.first, update.second);

    // Value-only edits do not change graph topology. Rebuilding every open
    // MaterialGraph here was doing a complete node-interface walk for no visual
    // benefit. Keep the graph objects untouched and refresh only the property
    // inspector and swatches. Structural notices still go through refresh(),
    // which calls refreshGraphs().
    updateSelection();
    d.ui->browserWidget->refreshVisibleSwatches();
    qDebug().noquote() << "[MaterialPerf][Dialog] updatePrims values FAST" << batch.entries.size() << "notice entries"
                       << dirtyMaterials.size() << "materials" << dirtyProperties.size() << "properties"
                       << timer.elapsed() << "ms";
}


void
MaterialDialogPrivate::openMaterialGraph(const SdfPath& materialPath)
{
    if (materialPath.IsEmpty())
        return;

    const QString key = QString::fromStdString(materialPath.GetString());
    if (MaterialGraph* existing = d.graphs.value(key)) {
        if (d.graphView)
            d.graphView->hide();
        if (d.tabs)
            d.tabs->show();
        const int index = d.tabs->indexOf(existing);
        if (index >= 0)
            d.tabs->setCurrentIndex(index);
        return;
    }

    const int row = d.ui->browserWidget->rowForMaterialPath(materialPath);
    const MaterialEntry* entry = d.ui->browserWidget->entry(row);
    if (!entry)
        return;

    const QList<MaterialEntry> selected = d.ui->browserWidget->selectedEntries();
    if (selected.size() != 1 || selected.first().materialPath != materialPath)
        d.ui->browserWidget->selectRow(row);

    auto* graph = new MaterialGraph(d.tabs);
    graph->setAcceptDrops(true);
    graph->installEventFilter(this);
    if (graph->viewport()) {
        graph->viewport()->setAcceptDrops(true);
        graph->viewport()->installEventFilter(this);
    }
    graph->setMaterial(*entry);
    d.graphs.insert(key, graph);
    // connect
    connect(graph, &MaterialGraph::nodeSelected, this, [this](const SdfPath& path) { selectGraphNode(path); });
    connect(graph, &MaterialGraph::connectionRequested, this,
            [this](const SdfPath& input, const SdfPath& output) { connectGraphSockets(input, output); });
    connect(graph, &MaterialGraph::disconnectRequested, this,
            [this](const SdfPath& input) { disconnectInputs({ input }); });
    connect(graph, &MaterialGraph::connectShaderNodeRequested, this,
            [this](const SdfPath& input, const QString& shaderId, const QString& nodeName, const TfToken& outputName) {
                connectShaderNode(input, shaderId, nodeName, outputName);
            });
    connect(graph, &MaterialGraph::connectMaterialXNodeRequested, this,
            [this](const SdfPath& input, const QString& nodeDef, const QString& nodeName) {
                connectMaterialXNode(input, nodeDef, nodeName);
            });
    if (d.graphView)
        d.graphView->hide();
    d.tabs->show();

    const int index = d.tabs->addTab(graph, entry->name);
    d.tabs->setCurrentIndex(index);

    selectGraphNode(entry->shaderPath);
}

void
MaterialDialogPrivate::closeMaterialGraph(int index)
{
    if (index < 0 || index >= d.tabs->count())
        return;

    QWidget* page = d.tabs->widget(index);
    if (!page)
        return;

    auto* graph = qobject_cast<MaterialGraph*>(page);
    if (!graph)
        return;

    const QString key = QString::fromStdString(graph->materialPath().GetString());

    d.graphs.remove(key);

    d.tabs->removeTab(index);
    graph->deleteLater();

    if (d.tabs->count() == 0) {
        d.tabs->hide();
        if (d.graphView)
            d.graphView->show();
    }
}

MaterialGraph*
MaterialDialogPrivate::currentGraph() const
{
    return d.tabs ? qobject_cast<MaterialGraph*>(d.tabs->currentWidget()) : nullptr;
}

void
MaterialDialogPrivate::refreshGraphs()
{
    if (!d.tabs)
        return;

    // Walk tabs backwards because closeMaterialGraph() removes the tab and the
    // corresponding entry from d.graphs. A material deleted from the stage must
    // never leave an orphaned graph tab behind.
    for (int index = d.tabs->count() - 1; index >= 0; --index) {
        auto* graph = qobject_cast<MaterialGraph*>(d.tabs->widget(index));
        if (!graph)
            continue;

        const int row = d.ui->browserWidget->rowForMaterialPath(graph->materialPath());
        const MaterialEntry* entry = d.ui->browserWidget->entry(row);

        if (!entry) {
            closeMaterialGraph(index);
            continue;
        }

        graph->setMaterial(*entry);
        d.tabs->setTabText(index, entry->name);
    }

    // Keep the lookup clean if a graph QObject was destroyed independently of
    // its tab for any reason.
    for (auto it = d.graphs.begin(); it != d.graphs.end();) {
        if (!it.value())
            it = d.graphs.erase(it);
        else
            ++it;
    }
}

void
MaterialDialogPrivate::selectGraphNode(const SdfPath& path)
{
    if (path.IsEmpty())
        return;

    QElapsedTimer timer;
    timer.start();

    if (MaterialGraph* graph = currentGraph()) {
        const int row = d.ui->browserWidget->rowForMaterialPath(graph->materialPath());
        const QList<MaterialEntry> selected = d.ui->browserWidget->selectedEntries();
        if (row >= 0 && (selected.size() != 1 || selected.first().materialPath != graph->materialPath()))
            d.ui->browserWidget->selectRow(row);
    }

    // Graph selection is authoritative for the large preview. Resolve the new
    // node immediately so an Image-node preview can never leak into the next
    // selected surface/helper node while MaterialTree is rebuilding.
    d.previewNode = path;
    d.previewNodeInfo = {};

    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (stage)
            d.previewNodeInfo = MaterialUtils::nodeInfo(stage, path);
    }

    if (!d.previewNodeInfo.path.IsEmpty()) {
        updateNodeHeader(d.previewNodeInfo.path, d.previewNodeInfo.name,
                         d.previewNodeInfo.typeLabel.isEmpty() ? QStringLiteral("UsdShade node")
                                                               : d.previewNodeInfo.typeLabel,
                         d.previewNodeInfo.shaderId);
    }

    updatePropertySwatch();

    const qint64 beforeTreeMs = timer.elapsed();
    d.ui->tree->navigateToNode(path);
    const qint64 treeMs = timer.elapsed() - beforeTreeMs;

    qDebug().noquote() << "[MaterialPerf][Dialog] selectGraphNode" << QString::fromStdString(path.GetString())
                       << (d.previewNodeInfo.shaderId.isEmpty() ? QStringLiteral("<unknown>")
                                                                : d.previewNodeInfo.shaderId)
                       << "tree" << treeMs << "ms"
                       << "total" << timer.elapsed() << "ms";
}


void
MaterialDialogPrivate::connectGraphSockets(const SdfPath& inputPath, const SdfPath& sourceOutputPath)
{
    QElapsedTimer timer;
    timer.start();
    if (inputPath.IsEmpty() || sourceOutputPath.IsEmpty() || !ensureInputs({ inputPath }))
        return;
    d.swatchSnapshotDirty = true;
    d.graphTopologyDirty = true;
    session()->commandStack()->run(new Command(connectShaderInput(inputPath, sourceOutputPath)));
    // UsdShade connection edits do not necessarily generate a prim-resync notice.
    // Force a topology refresh so rebuildEdges() sees the authored connection and
    // the new wire becomes visible immediately.
    d.refreshTimer->start();
    qDebug().noquote() << "[MaterialPerf][Dialog] connectGraphSockets"
                       << QString::fromStdString(sourceOutputPath.GetString()) << "->"
                       << QString::fromStdString(inputPath.GetString()) << timer.elapsed() << "ms";
}

void
MaterialDialogPrivate::updateNodeHeader(const SdfPath& path, const QString& name, const QString& type,
                                        const QString& shaderId)
{
    Q_UNUSED(path);

    QString title = name;
    if (title.isEmpty())
        title = QStringLiteral("Material");

    const QString typeText = type.isEmpty() ? shaderId : type;
    const QColor textAlt = style()->color(Style::ColorRole::TextAlt);

    d.ui->name->setText(QStringLiteral("<b>%1</b><br><span style='color:%2'>%3</span>")
                            .arg(title.toHtmlEscaped(), textAlt.name(QColor::HexRgb), typeText.toHtmlEscaped()));
    d.ui->name->setVisible(true);
    d.ui->name->setToolTip(QStringLiteral("%1\n%2").arg(typeText, shaderId));
}

void
MaterialDialogPrivate::updateSelection()
{
    const QList<MaterialEntry> entries = d.ui->browserWidget->selectedEntries();

    if (entries.isEmpty()) {
        d.previewNode = {};
        d.previewNodeInfo = {};
        d.ui->name->clear();
        d.ui->name->setToolTip(QString());
        d.ui->name->setVisible(false);
        d.ui->tree->clearMaterials();
        updatePropertySwatch();
        return;
    }

    d.ui->name->setVisible(true);
    d.ui->tree->setMaterials(entries);

    if (entries.size() == 1) {
        const MaterialEntry& entry = entries.first();

        // If the active graph belongs to this material, preserve its selected
        // node as the property-preview target across material refreshes. The
        // browser refresh path must not silently replace an Image-node preview
        // with the master material swatch/header.
        bool restoredGraphNode = false;
        if (MaterialGraph* graph = currentGraph()) {
            if (graph->materialPath() == entry.materialPath && !d.previewNode.IsEmpty()
                && d.previewNode.GetPrimPath().HasPrefix(entry.materialPath)) {
                const MaterialNodeInfo node = d.ui->tree->currentNodeInfo();
                if (node.path == d.previewNode) {
                    d.previewNodeInfo = node;
                    updateNodeHeader(node.path, node.name,
                                     node.typeLabel.isEmpty() ? QStringLiteral("UsdShade node") : node.typeLabel,
                                     node.shaderId);
                    restoredGraphNode = true;
                }
            }
        }

        if (!restoredGraphNode) {
            d.previewNode = entry.shaderPath;
            const MaterialNodeInfo node = d.ui->tree->currentNodeInfo();
            d.previewNodeInfo = node.path == entry.shaderPath ? node : MaterialNodeInfo();
            const QString type = MaterialUtils::shaderTypeLabel(entry.shaderId);
            const QColor textAlt = style()->color(Style::ColorRole::TextAlt);
            const QString typeText = type.isEmpty() ? entry.shaderId : type;

            d.ui->name->setText(
                QStringLiteral("<b>%1</b><br><span style='color:%2'>%3</span>")
                    .arg(entry.name.toHtmlEscaped(), textAlt.name(QColor::HexRgb), typeText.toHtmlEscaped()));
            d.ui->name->setToolTip(
                QStringLiteral("%1\n%2").arg(typeText, QString::fromStdString(entry.materialPath.GetString())));
        }

        // Single-click is property inspection only. Graph tabs are opened by
        // double-clicking a material in the browser.
    }
    else {
        d.previewNode = {};
        d.previewNodeInfo = {};
        const QColor textAlt = style()->color(Style::ColorRole::TextAlt);

        d.ui->name->setText(
            QStringLiteral("<b>%1 materials</b><br><span style='color:%2'>Common editable properties</span>")
                .arg(entries.size())
                .arg(textAlt.name(QColor::HexRgb)));

        d.ui->name->setToolTip(QStringLiteral("Editing common supported values"));
    }
    updatePropertySwatch();
}

QImage
MaterialDialogPrivate::imageNodePreview(const MaterialNodeInfo& node)
{
    if (node.path.IsEmpty())
        return {};

    QString filename;
    QString unresolvedFilename;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return {};

        // Only known texture-producing shader nodes get a standalone 2D
        // preview. Do not classify a node merely because it happens to expose a
        // filename/asset parameter: many MaterialX utility nodes can carry file
        // references that are not image data. Keep this identity based so the
        // preview contract is explicit and predictable.
        const QString shaderId = node.shaderId;
        const bool materialXImage = shaderId.startsWith(QStringLiteral("ND_image_"))
                                    || shaderId.startsWith(QStringLiteral("ND_tiledimage_"));
        const bool usdPreviewImage = shaderId == QStringLiteral("UsdUVTexture");
        if (!materialXImage && !usdPreviewImage)
            return {};

        // Once the node itself is known to be a texture node, locate its source
        // file robustly. OpenUSD/MaterialX versions can expose this slot as
        // `file`, `filename`, an Asset, String or Token; the UI label may also be
        // localized/humanized (for example "Filename").
        const MaterialInputInfo* fileInput = nullptr;
        for (const MaterialInputInfo& input : node.inputs) {
            const QString inputName = QString::fromStdString(input.inputName.GetString()).toLower();
            const QString parameter = input.parameter.toLower();
            const QString label = input.label.toLower();
            const bool nameMatches = inputName == QStringLiteral("file") || inputName == QStringLiteral("filename")
                                     || parameter == QStringLiteral("file") || parameter == QStringLiteral("filename")
                                     || label == QStringLiteral("file") || label == QStringLiteral("filename");
            const bool typeMatches = input.typeName == SdfValueTypeNames->Asset
                                     || input.typeName == SdfValueTypeNames->String
                                     || input.typeName == SdfValueTypeNames->Token;
            if (nameMatches && typeMatches) {
                fileInput = &input;
                break;
            }
        }

        if (fileInput && fileInput->hasValue && !fileInput->value.IsEmpty()) {
            const MaterialInputInfo& input = *fileInput;
            std::string assetPath;
            if (input.value.IsHolding<SdfAssetPath>()) {
                const SdfAssetPath asset = input.value.UncheckedGet<SdfAssetPath>();
                if (!asset.GetResolvedPath().empty())
                    filename = QString::fromStdString(asset.GetResolvedPath());
                assetPath = asset.GetAssetPath();
            }
            else if (input.value.IsHolding<std::string>()) {
                assetPath = input.value.UncheckedGet<std::string>();
            }
            else if (input.value.IsHolding<TfToken>()) {
                assetPath = input.value.UncheckedGet<TfToken>().GetString();
            }

            if (!assetPath.empty()) {
                unresolvedFilename = QString::fromStdString(assetPath);
                if (filename.isEmpty()) {
                    ArResolverContextBinder binder(stage->GetPathResolverContext());
                    const auto resolved = ArGetResolver().Resolve(assetPath);
                    if (!resolved.empty())
                        filename = QString::fromStdString(resolved.GetPathString());
                }

                // A composed asset value can still have an empty resolved path
                // when the authored path is relative. Anchor it against the
                // layer that actually authored the input before falling back to
                // the raw string. This is the USD-correct fallback for sublayers,
                // references and saved stages.
                if (filename.isEmpty() && input.inputPath.IsPropertyPath()) {
                    const UsdAttribute attribute = stage->GetAttributeAtPath(input.inputPath);
                    if (attribute) {
                        for (const SdfPropertySpecHandle& spec : attribute.GetPropertyStack()) {
                            if (!spec || !spec->GetLayer())
                                continue;
                            const std::string anchored = SdfComputeAssetPathRelativeToLayer(spec->GetLayer(),
                                                                                            assetPath);
                            if (anchored.empty())
                                continue;
                            ArResolverContextBinder binder(stage->GetPathResolverContext());
                            const auto resolved = ArGetResolver().Resolve(anchored);
                            if (!resolved.empty()) {
                                filename = QString::fromStdString(resolved.GetPathString());
                                break;
                            }
                            const QString candidate = QString::fromStdString(anchored);
                            if (QFileInfo::exists(candidate)) {
                                filename = candidate;
                                break;
                            }
                        }
                    }
                }

                if (filename.isEmpty())
                    filename = unresolvedFilename;
            }
        }
    }

    if (filename.isEmpty()) {
        QStringList inputs;
        for (const MaterialInputInfo& input : node.inputs) {
            inputs << QStringLiteral("%1:%2:%3")
                          .arg(QString::fromStdString(input.inputName.GetString()),
                               QString::fromStdString(input.typeName.GetAsToken().GetString()),
                               input.hasValue ? QStringLiteral("value") : QStringLiteral("unset"));
        }
        qDebug().noquote() << "[MaterialPerf][Dialog] imageNodePreview no file" << node.shaderId
                           << QString::fromStdString(node.path.GetString()) << "inputs"
                           << inputs.join(QStringLiteral(", "));
        return {};
    }

    QSize canvasSize = d.ui && d.ui->swatch ? d.ui->swatch->size() : QSize(512, 320);
    if (canvasSize.width() < 64 || canvasSize.height() < 64)
        canvasSize = QSize(512, 320);

    const QFileInfo fileInfo(filename);
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4x%5")
                                 .arg(fileInfo.absoluteFilePath())
                                 .arg(fileInfo.size())
                                 .arg(fileInfo.lastModified().toMSecsSinceEpoch())
                                 .arg(canvasSize.width())
                                 .arg(canvasSize.height());

    if (const auto found = d.texturePreviewCache.constFind(cacheKey); found != d.texturePreviewCache.cend()) {
        d.texturePreviewCacheOrder.removeAll(cacheKey);
        d.texturePreviewCacheOrder.append(cacheKey);
        return found.value();
    }

    QString decodeError;
    QString sourceColorSpace;
    const QImage source = loadTextureWithOpenImageIO(filename, &decodeError, &sourceColorSpace);
    if (source.isNull()) {
        qDebug().noquote() << "[MaterialPerf][Dialog] imageNodePreview load FAILED" << node.shaderId << filename
                           << "unresolved" << unresolvedFilename << "error" << decodeError;
        return {};
    }

    QImage result(canvasSize, QImage::Format_RGBA8888);
    result.fill(style()->color(Style::ColorRole::Render));

    constexpr int margin = 16;
    const QSize available(std::max(1, canvasSize.width() - margin * 2), std::max(1, canvasSize.height() - margin * 2));
    const QImage scaled = source.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft((canvasSize.width() - scaled.width()) / 2, (canvasSize.height() - scaled.height()) / 2);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(topLeft, scaled);
    painter.end();

    d.texturePreviewCache.insert(cacheKey, result);
    d.texturePreviewCacheOrder.removeAll(cacheKey);
    d.texturePreviewCacheOrder.append(cacheKey);
    while (d.texturePreviewCacheOrder.size() > 32)
        d.texturePreviewCache.remove(d.texturePreviewCacheOrder.takeFirst());

    qDebug().noquote() << "[MaterialPerf][Dialog] imageNodePreview" << node.shaderId << filename << "sourceColorSpace"
                       << sourceColorSpace << source.size() << "->" << result.size();
    return result;
}

void
MaterialDialogPrivate::updatePropertySwatch()
{
    const QList<MaterialEntry> entries = d.ui->browserWidget->selectedEntries();
    if (entries.size() != 1) {
        d.ui->swatch->setMaterial(QImage(), QString());
        d.ui->swatch->setToolTip(QString());
        return;
    }

    const MaterialEntry& entry = entries.first();
    const QString materialPath = QString::fromStdString(entry.materialPath.GetString());

    // The preview image and drag material path are updated together. This keeps
    // the large swatch bound to exactly the material it represents, even when
    // the displayed image is temporarily a texture/node preview.
    const QImage texturePreview = imageNodePreview(d.previewNodeInfo);
    if (!texturePreview.isNull()) {
        d.ui->swatch->setToolTip(QString::fromStdString(d.previewNode.GetString()));
        d.ui->swatch->setMaterial(texturePreview, materialPath);
        return;
    }

    // The material's master surface shader still represents the complete
    // material, so keep the rendered shaderball for that node.
    if (!d.previewNode.IsEmpty() && d.previewNode != entry.shaderPath) {
        QSize canvasSize = d.ui->swatch->size();
        if (canvasSize.width() < 1 || canvasSize.height() < 1)
            canvasSize = QSize(512, 320);

        QImage black(canvasSize, QImage::Format_RGBA8888);
        black.fill(Qt::black);
        d.ui->swatch->setToolTip(QString::fromStdString(d.previewNode.GetString()));
        d.ui->swatch->setMaterial(black, materialPath);
        return;
    }

    const int row = d.ui->browserWidget->rowForMaterialPath(entry.materialPath);
    const QImage image = row >= 0 ? d.ui->browserWidget->swatch(row) : QImage();

    d.ui->swatch->setToolTip(materialPath);
    d.ui->swatch->setMaterial(image, materialPath);
}

bool
MaterialDialogPrivate::ensureSwatchSnapshot()
{
    QElapsedTimer timer;
    timer.start();
    if (!d.swatchSnapshot) {
        d.swatchSnapshot = SdfLayer::CreateAnonymous("stageviz_material_snapshot.usda");
        d.swatchSnapshotDirty = true;
    }

    if (!d.swatchSnapshotDirty)
        return true;

    SdfLayerRefPtr flattened;
    qint64 flattenMs = 0;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (stage) {
            QElapsedTimer flattenTimer;
            flattenTimer.start();
            flattened = stage->Flatten();
            flattenMs = flattenTimer.elapsed();
        }
    }

    if (!flattened)
        return false;

    d.swatchSnapshot->TransferContent(flattened);
    d.swatchSnapshotDirty = false;

    // The fresh composed snapshot already contains every committed value. Drop
    // the lightweight root-layer overrides accumulated since the previous full
    // structural sync so they cannot shadow newer topology/defaults.
    d.renderer->clearOverrides();
    qDebug().noquote() << "[MaterialPerf][Dialog] swatchSnapshot rebuild"
                       << "flatten" << flattenMs << "ms"
                       << "total" << timer.elapsed() << "ms";
    return true;
}

void
MaterialDialogPrivate::requestSwatch(int row)
{
    const MaterialEntry* entry = d.ui->browserWidget->entry(row);
    if (!entry || !ensureSwatchSnapshot())
        return;

    // Swatches always represent the complete material network. The selected graph
    // node is editor state only and never changes what is rendered here.
    d.renderer->request(entry->materialPath, d.swatchSnapshot);
}


void
MaterialDialogPrivate::updateSwatch(const QString& materialPath, const QImage& image)
{
    const int row = d.ui->browserWidget->rowForMaterialPath(SdfPath(materialPath.toStdString()));
    if (row >= 0) {
        d.ui->browserWidget->setSwatch(row, image);
        updatePropertySwatch();
    }
}

void
MaterialDialogPrivate::previewFloat(const QString& parameter, double value)
{
    if (!ensureSwatchSnapshot())
        return;

    const QList<int> rows = d.ui->browserWidget->selectedRows();
    for (int row : rows) {
        const MaterialEntry* entry = d.ui->browserWidget->entry(row);
        if (!entry || !MaterialUtils::isSupportedParameter(*entry, parameter))
            continue;

        const SdfPath inputPath = MaterialUtils::inputPath(*entry, parameter);
        if (inputPath.IsEmpty())
            continue;

        VtValue authored;
        if (parameter == QStringLiteral("opacity")
            && entry->shaderId == QStringLiteral("ND_standard_surface_surfaceshader")) {
            authored = VtValue(GfVec3f(static_cast<float>(value)));
        }
        else {
            authored = VtValue(static_cast<float>(value));
        }

        // Interactive rendering uses the same complete UsdShade/MaterialX graph
        // as the final swatch, only at a smaller render target. No fallback shader
        // or currently-selected-node preview is involved.
        d.renderer->preview(entry->materialPath, d.swatchSnapshot, inputPath, authored);
    }
}

void
MaterialDialogPrivate::previewColor(const QString& parameter, const QColor& value)
{
    if (!ensureSwatchSnapshot())
        return;

    const QList<int> rows = d.ui->browserWidget->selectedRows();
    const VtValue authored(GfVec3f(value.redF(), value.greenF(), value.blueF()));

    for (int row : rows) {
        const MaterialEntry* entry = d.ui->browserWidget->entry(row);
        if (!entry || !MaterialUtils::isSupportedParameter(*entry, parameter))
            continue;

        const SdfPath inputPath = MaterialUtils::inputPath(*entry, parameter);
        if (!inputPath.IsEmpty())
            d.renderer->preview(entry->materialPath, d.swatchSnapshot, inputPath, authored);
    }
}


void
MaterialDialogPrivate::previewFloatInputs(const QList<SdfPath>& inputPaths, double value)
{
    if (inputPaths.isEmpty() || !ensureSwatchSnapshot())
        return;

    const QList<MaterialEntry>& entries = d.ui->browserWidget->entries();
    for (const SdfPath& inputPath : inputPaths) {
        if (inputPath.IsEmpty())
            continue;

        const SdfPath primPath = inputPath.GetPrimPath();
        const MaterialEntry* material = nullptr;
        for (const MaterialEntry& entry : entries) {
            if (primPath.HasPrefix(entry.materialPath)) {
                material = &entry;
                break;
            }
        }
        if (!material)
            continue;

        d.renderer->preview(material->materialPath, d.swatchSnapshot, inputPath, VtValue(static_cast<float>(value)));
    }
}

void
MaterialDialogPrivate::previewColorInputs(const QList<SdfPath>& inputPaths, const QColor& value)
{
    if (inputPaths.isEmpty() || !ensureSwatchSnapshot())
        return;

    const VtValue authored(GfVec3f(value.redF(), value.greenF(), value.blueF()));
    const QList<MaterialEntry>& entries = d.ui->browserWidget->entries();
    for (const SdfPath& inputPath : inputPaths) {
        if (inputPath.IsEmpty())
            continue;

        const SdfPath primPath = inputPath.GetPrimPath();
        const MaterialEntry* material = nullptr;
        for (const MaterialEntry& entry : entries) {
            if (primPath.HasPrefix(entry.materialPath)) {
                material = &entry;
                break;
            }
        }
        if (!material)
            continue;

        d.renderer->preview(material->materialPath, d.swatchSnapshot, inputPath, authored);
    }
}

void
MaterialDialogPrivate::editFloat(const QString& parameter, double value)
{
    const QList<MaterialEntry> entries = d.ui->browserWidget->selectedEntries();
    QList<SdfPath> paths;

    for (const MaterialEntry& entry : entries) {
        if (!MaterialUtils::isSupportedParameter(entry, parameter))
            continue;

        const SdfPath path = MaterialUtils::inputPath(entry, parameter);
        if (!path.IsEmpty())
            paths.append(path);
    }

    if (paths.isEmpty())
        return;

    VtValue authored;
    if (parameter == "opacity") {
        const bool standardOnly = std::all_of(entries.cbegin(), entries.cend(), [](const MaterialEntry& entry) {
            return entry.shaderId == "ND_standard_surface_surfaceshader";
        });

        authored = standardOnly ? VtValue(GfVec3f(static_cast<float>(value))) : VtValue(static_cast<float>(value));
    }
    else {
        authored = VtValue(static_cast<float>(value));
    }

    session()->commandStack()->run(new Command(setAttributeValues(paths, authored)));
}

void
MaterialDialogPrivate::editColor(const QString& parameter, const QColor& value)
{
    const QList<MaterialEntry> entries = d.ui->browserWidget->selectedEntries();
    QList<SdfPath> paths;

    for (const MaterialEntry& entry : entries) {
        if (!MaterialUtils::isSupportedParameter(entry, parameter))
            continue;

        const SdfPath path = MaterialUtils::inputPath(entry, parameter);
        if (!path.IsEmpty())
            paths.append(path);
    }

    if (paths.isEmpty())
        return;

    const GfVec3f color(value.redF(), value.greenF(), value.blueF());
    session()->commandStack()->run(new Command(setAttributeValues(paths, VtValue(color))));
}

bool
MaterialDialogPrivate::ensureInputs(const QList<SdfPath>& inputPaths)
{
    if (inputPaths.isEmpty())
        return false;

    WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
    const UsdStageRefPtr stage = session()->stageUnsafe();
    if (!stage)
        return false;

    for (const SdfPath& path : inputPaths) {
        if (!stage->GetAttributeAtPath(path) && !MaterialUtils::ensureShaderInput(stage, path))
            return false;
    }
    return true;
}

void
MaterialDialogPrivate::editFloatInputs(const QList<SdfPath>& inputPaths, double value)
{
    if (!ensureInputs(inputPaths))
        return;
    session()->commandStack()->run(new Command(setAttributeValues(inputPaths, VtValue(static_cast<float>(value)))));
}

void
MaterialDialogPrivate::editColorInputs(const QList<SdfPath>& inputPaths, const QColor& value)
{
    if (!ensureInputs(inputPaths))
        return;
    const GfVec3f color(value.redF(), value.greenF(), value.blueF());
    session()->commandStack()->run(new Command(setAttributeValues(inputPaths, VtValue(color))));
}

void
MaterialDialogPrivate::disconnectInputs(const QList<SdfPath>& inputPaths)
{
    if (!inputPaths.isEmpty()) {
        d.swatchSnapshotDirty = true;
        d.graphTopologyDirty = true;
        session()->commandStack()->run(new Command(disconnectShaderInputs(inputPaths)));
        // A connection edit usually arrives as a property-value notice rather
        // than a prim resync. Schedule the structural refresh explicitly so the
        // graph edge disappears immediately instead of taking the value fast path.
        d.refreshTimer->start();
    }
}

void
MaterialDialogPrivate::resetInputs(const QList<SdfPath>& inputPaths)
{
    if (!inputPaths.isEmpty()) {
        // A reset may reveal a weaker opinion/default that is not represented by
        // the incremental override cache, so refresh the composed network snapshot.
        d.swatchSnapshotDirty = true;
        // Reset the authored value only. Do not remove the UsdShade input
        // property: NodeDef/schema-defined slots must remain part of the node
        // interface after a reset. This matches PropertyTree's Reset Value
        // semantics.
        session()->commandStack()->run(new Command(resetAttributeValues(inputPaths)));
    }
}

void
MaterialDialogPrivate::connectShaderNode(const SdfPath& inputPath, const QString& shaderId, const QString& nodeName,
                                         const TfToken& outputName)
{
    QElapsedTimer timer;
    timer.start();
    bool executed = false;
    if (!inputPath.IsEmpty() && !shaderId.isEmpty() && !outputName.IsEmpty() && ensureInputs({ inputPath })) {
        d.swatchSnapshotDirty = true;
        d.graphTopologyDirty = true;
        session()->commandStack()->run(
            new Command(stageviz::connectShaderNode(inputPath, shaderId, nodeName, outputName)));
        d.refreshTimer->start();
        executed = true;
    }
    qDebug().noquote() << "[MaterialPerf][Dialog] connectShaderNode" << shaderId << nodeName
                       << QString::fromStdString(inputPath.GetString()) << "executed" << executed << timer.elapsed()
                       << "ms";
}

void
MaterialDialogPrivate::connectMaterialXNode(const SdfPath& inputPath, const QString& nodeDef, const QString& nodeName)
{
    QElapsedTimer timer;
    timer.start();
    bool executed = false;
    if (!inputPath.IsEmpty() && !nodeDef.isEmpty() && ensureInputs({ inputPath })) {
        d.swatchSnapshotDirty = true;
        d.graphTopologyDirty = true;
        session()->commandStack()->run(new Command(stageviz::connectMaterialXNode(inputPath, nodeDef, nodeName)));
        d.refreshTimer->start();
        executed = true;
    }
    qDebug().noquote() << "[MaterialPerf][Dialog] connectMaterialXNode" << nodeDef << nodeName
                       << QString::fromStdString(inputPath.GetString()) << "executed" << executed << timer.elapsed()
                       << "ms";
}

void
MaterialDialogPrivate::createPreviewSurface()
{
    SdfPath path;
    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        path = MaterialUtils::createPreviewSurfaceMaterial(session()->stageUnsafe());
    }

    if (!path.IsEmpty())
        d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::createStandardSurface()
{
    SdfPath path;
    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        path = MaterialUtils::createStandardSurfaceMaterial(session()->stageUnsafe());
    }

    if (!path.IsEmpty())
        d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::createOpenPBRSurface()
{
    SdfPath path;
    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        path = MaterialUtils::createOpenPBRSurfaceMaterial(session()->stageUnsafe());
    }

    if (!path.IsEmpty())
        d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::loadMaterialX()
{
    const QString directory = settings()->value("materialXDir", QDir::homePath()).toString();

    // Use the platform's normal file picker. QFileDialog::getOpenFileName()
    // uses the native dialog when Qt/platform integration provides one.
    const QString filename = QFileDialog::getOpenFileName(d.dialog, tr("Load MaterialX"), directory,
                                                          tr("MaterialX (*.mtlx);;All Files (*)"));

    if (filename.isEmpty())
        return;
    QList<SdfPath> created;
    QString error;

    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        if (!MaterialUtils::importMaterialX(session()->stageUnsafe(), filename, created, error)) {
            setStatus(error);
            return;
        }
    }

    settings()->setValue("materialXDir", QFileInfo(filename).absolutePath());
    d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::applyToSelection()
{
    const QList<MaterialEntry> materials = d.ui->browserWidget->selectedEntries();
    if (materials.size() != 1)
        return;

    const QList<SdfPath> selectedPaths = session()->selectionList()->paths();
    if (selectedPaths.isEmpty())
        return;

    QList<SdfPath> paths;
    QSet<QString> seen;

    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        for (const SdfPath& selectedPath : selectedPaths) {
            const SdfPath primPath = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
            const QString key = QString::fromStdString(primPath.GetString());

            if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath() || seen.contains(key))
                continue;

            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim || !prim.IsValid())
                continue;

            seen.insert(key);
            paths.append(primPath);
        }
    }

    if (!paths.isEmpty())
        session()->commandStack()->run(new Command(bindMaterial(paths, materials.first().materialPath)));
}

void
MaterialDialogPrivate::selectFromStage()
{
    const QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.isEmpty()) {
        d.ui->browserWidget->selectRows({});
        return;
    }

    QSet<QString> materialPaths;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage) {
            d.ui->browserWidget->selectRows({});
            return;
        }

        for (const SdfPath& path : paths) {
            const UsdPrim prim = stage->GetPrimAtPath(path.IsPropertyPath() ? path.GetPrimPath() : path);
            if (!prim)
                continue;

            const UsdShadeMaterial bound = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
            if (bound)
                materialPaths.insert(QString::fromStdString(bound.GetPath().GetString()));
        }
    }

    QList<int> rows;
    for (const QString& path : materialPaths) {
        const int row = d.ui->browserWidget->rowForMaterialPath(SdfPath(path.toStdString()));
        if (row >= 0)
            rows.append(row);
    }

    std::sort(rows.begin(), rows.end());
    d.ui->browserWidget->selectRows(rows);
}

void
MaterialDialogPrivate::deleteMaterials()
{
    const QList<MaterialEntry> materials = d.ui->browserWidget->selectedEntries();
    if (materials.isEmpty())
        return;

    QList<SdfPath> paths;
    paths.reserve(materials.size());

    for (const MaterialEntry& material : materials)
        paths.append(material.materialPath);

    // Deleting a material changes graph topology. Mark it dirty before the
    // command runs so the next structural refresh also prunes any open tab for
    // the removed material.
    d.graphTopologyDirty = true;
    session()->commandStack()->run(new Command(deletePaths(paths)));
}

void
MaterialDialogPrivate::renameMaterial(const SdfPath& path, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (path.IsEmpty() || trimmed.isEmpty())
        return;

    SdfPath destination;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr usdStage = session()->stageUnsafe();
        if (!usdStage)
            return;

        QString error;
        destination = stage::buildRenamePath(usdStage, path, trimmed, error);
    }

    if (destination.IsEmpty() || destination == path)
        return;

    d.pendingRenameSource = path;
    d.pendingRenameDestination = destination;
    d.pendingMaterialSelection = destination;

    session()->commandStack()->run(new Command(renamePath(path, trimmed)));
}

MaterialDialog::MaterialDialog(QWidget* parent)
    : QDialog(parent)
    , p(new MaterialDialogPrivate(this))
{}

MaterialDialog::~MaterialDialog() = default;

}  // namespace stageviz