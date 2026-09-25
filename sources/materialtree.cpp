// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialtree.h"

#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "materialmenu.h"
#include "materialutils.h"
#include "session.h"
#include "spinbox.h"
#include "style.h"
#include "tracelocks.h"
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <utility>

namespace stageviz {

namespace {
    constexpr int kMinimumPropertyColumnWidth = 120;
    constexpr int kActionRole = Qt::UserRole + 20;
    constexpr int kActionPathRole = Qt::UserRole + 21;
    constexpr int kGroupNameRole = Qt::UserRole + 22;
    constexpr int kGroupPopulatedRole = Qt::UserRole + 23;
    enum TreeAction { NoTreeAction = 0, AddNodeAction = 1, OpenNodeAction = 2 };
}  // namespace

class MaterialTreePrivate : public QObject {
public:
    void ensureInitialized();
    void init();
    void rebuild(bool preserveScroll = false);
    bool refreshSingleValues();
    bool sameNodeStructure(const MaterialNodeInfo& a, const MaterialNodeInfo& b) const;
    QTreeWidgetItem* inputItem(const SdfPath& path) const;
    void refreshInputItem(QTreeWidgetItem* item, const MaterialInputInfo& info);
    void rebuildSingle();
    void rebuildMulti();
    void populateGroup(QTreeWidgetItem* group);
    void captureExpansionState();
    void addNodeInfo(const MaterialNodeInfo& node);
    void addInputRow(QTreeWidgetItem* group, const MaterialInputInfo& info, const QList<MaterialInputInfo>& aggregate);
    void navigateTo(const SdfPath& path, bool pushHistory = true);
    void back();
    void forward();
    void showInputMenu(const QPoint& position);
    void showAddNodeMenu(const MaterialInputInfo& info, QWidget* anchor);
    QColor currentColor(const QString& parameter) const;
    void setInputValues(const QList<SdfPath>& inputPaths, const VtValue& value);
    void updateColumnWidths();
    bool eventFilter(QObject* object, QEvent* event) override;

public:
    class FloatControl : public SpinBox {
    public:
        FloatControl(double minimum, double maximum, double step, QWidget* parent = nullptr)
            : SpinBox(parent)
        {
            setRange(minimum, maximum);
            setSingleStep(step);
            setDecimals(3);
            setMinimumWidth(72);
            connect(this, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
                if (!m_block && previewChanged)
                    previewChanged(value);
            });
            connect(this, &QDoubleSpinBox::editingFinished, this, [this]() { commit(value()); });
            connect(this, &SpinBox::scrubFinished, this, [this](double value) { commit(value); });
        }
        void setValue(double value, bool mixed)
        {
            m_block = true;
            const double clamped = std::clamp(value, minimum(), maximum());
            QDoubleSpinBox::setValue(clamped);
            setPrefix(mixed ? QStringLiteral("≈ ") : QString());
            m_lastCommitted = clamped;
            m_block = false;
        }
        std::function<void(double)> previewChanged;
        std::function<void(double)> changed;

    private:
        void commit(double value)
        {
            if (m_block)
                return;
            if (std::isfinite(m_lastCommitted) && std::abs(value - m_lastCommitted) <= 1e-12)
                return;
            m_lastCommitted = value;
            if (changed)
                changed(value);
        }

        double m_lastCommitted = std::numeric_limits<double>::quiet_NaN();
        bool m_block = false;
    };

    struct Data {
        QPointer<MaterialTree> tree;
        QList<MaterialEntry> materials;
        SdfPath currentNode;
        QList<SdfPath> history;
        int historyIndex = -1;
        QHash<QString, FloatControl*> colorControls;
        QHash<QTreeWidgetItem*, int> groupPropertyRows;
        QHash<QString, QSet<QString>> expandedGroupsByNode;
        QSet<QString> nodesWithExpansionState;
        QIcon iconLeft;
        QIcon iconRight;
        QIcon iconNew;
        QIcon iconOpen;
        MaterialNodeInfo displayedNode;
        bool adjustingColumns = false;
        bool initialized = false;
    };
    Data d;
};

void
MaterialTreePrivate::ensureInitialized()
{
    if (d.initialized)
        return;

    init();
    d.initialized = true;
}

void
MaterialTreePrivate::init()
{
    d.tree->setColumnCount(3);
    d.tree->setHeaderLabels({ "Property", "Value", "" });
    d.tree->header()->setVisible(true);
    d.tree->header()->setStretchLastSection(false);
    d.tree->header()->setSectionsMovable(true);
    d.tree->header()->setMinimumSectionSize(36);
    // Property absorbs all horizontal resizing from the parent panel. Value
    // and the action column are fixed so the header exposes no resize handles.
    d.tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    d.tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    d.tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);

    d.tree->header()->resizeSection(1, 120);
    d.tree->header()->resizeSection(2, 44);
    d.tree->setMinimumWidth(kMinimumPropertyColumnWidth + 120 + 44);

    connect(d.tree.data(), &QTreeWidget::itemExpanded, d.tree.data(), [this](QTreeWidgetItem* item) {
        if (!item || !item->data(0, kGroupPopulatedRole).isValid() || d.currentNode.IsEmpty())
            return;
        const QString groupName = item->data(0, kGroupNameRole).toString();
        populateGroup(item);
        const QString nodeKey = QString::fromStdString(d.currentNode.GetString());
        d.nodesWithExpansionState.insert(nodeKey);
        d.expandedGroupsByNode[nodeKey].insert(groupName);
    });
    connect(d.tree.data(), &QTreeWidget::itemCollapsed, d.tree.data(), [this](QTreeWidgetItem* item) {
        if (!item || !item->data(0, kGroupPopulatedRole).isValid() || d.currentNode.IsEmpty())
            return;
        const QString groupName = item->data(0, kGroupNameRole).toString();
        const QString nodeKey = QString::fromStdString(d.currentNode.GetString());
        d.nodesWithExpansionState.insert(nodeKey);
        d.expandedGroupsByNode[nodeKey].remove(groupName);
    });

    // Resolve the small editor icons once. Rebuilding the property tree can create
    // dozens of buttons; asking Style for the same PNG-backed icon on every row
    // caused repeated image decoding and noisy libpng warnings in Debug builds.
    d.iconLeft = style()->icon(Style::IconRole::Left, Style::UIScale::Small);
    d.iconRight = style()->icon(Style::IconRole::Right, Style::UIScale::Small);
    d.iconNew = style()->icon(Style::IconRole::New, Style::UIScale::Small);
    d.iconOpen = style()->icon(Style::IconRole::Open, Style::UIScale::Small);

    d.tree->installEventFilter(this);
    d.tree->viewport()->installEventFilter(this);

    QTimer::singleShot(0, d.tree.data(), [this]() { updateColumnWidths(); });
    d.tree->setRootIsDecorated(true);
    d.tree->setUniformRowHeights(false);
    d.tree->setSelectionMode(QAbstractItemView::NoSelection);
    d.tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d.tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(d.tree.data(), &QWidget::customContextMenuRequested, d.tree.data(),
            [this](const QPoint& pos) { showInputMenu(pos); });

    // Node actions use the QTreeWidget cell itself instead of embedding one
    // QWidget/QToolButton per input row. Standard Surface has 40+ inputs and the
    // old widget-per-button path dominated full tree rebuild time.
    connect(d.tree.data(), &QTreeWidget::itemClicked, d.tree.data(), [this](QTreeWidgetItem* item, int column) {
        if (!item || column != 2)
            return;

        const int action = item->data(2, kActionRole).toInt();
        if (action == OpenNodeAction) {
            const SdfPath path(item->data(2, kActionPathRole).toString().toStdString());
            if (!path.IsEmpty())
                navigateTo(path);
            return;
        }

        if (action != AddNodeAction)
            return;

        const SdfPath inputPath(item->data(0, Qt::UserRole).toString().toStdString());
        if (inputPath.IsEmpty())
            return;

        for (const MaterialInputInfo& input : d.displayedNode.inputs) {
            if (input.inputPath == inputPath) {
                showAddNodeMenu(input, d.tree.data());
                return;
            }
        }
    });
    d.tree->setEnabled(false);
}

void
MaterialTreePrivate::updateColumnWidths()
{
    if (!d.tree || d.tree->columnCount() < 3)
        return;

    QHeaderView* header = d.tree->header();
    if (!header)
        return;

    // Value and action widths are intentionally fixed. Property is Stretch and
    // therefore takes all remaining space whenever the parent panel resizes.
    header->resizeSection(1, 120);
    header->resizeSection(2, 44);
}

bool
MaterialTreePrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event && event->type() == QEvent::Resize && (object == d.tree || (d.tree && object == d.tree->viewport()))) {
        updateColumnWidths();
    }

    return QObject::eventFilter(object, event);
}

static QTreeWidgetItem*
addGroup(QTreeWidget* tree, const QString& name)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, name);
    item->setFirstColumnSpanned(true);
    item->setExpanded(true);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    return item;
}

void
MaterialTreePrivate::addNodeInfo(const MaterialNodeInfo& node)
{
    QTreeWidgetItem* group = addGroup(d.tree.data(), QStringLiteral("Node"));

    auto addTextRow = [this, group](const QString& key, const QString& value, const QString& tooltip = QString()) {
        auto* item = new QTreeWidgetItem(group);
        item->setText(0, key);
        item->setText(1, value);
        if (!tooltip.isEmpty())
            item->setToolTip(1, tooltip);
        return item;
    };

    QTreeWidgetItem* nameItem = addTextRow(QStringLiteral("Name"), node.name);
    if (d.historyIndex > 0 || (d.historyIndex >= 0 && d.historyIndex + 1 < d.history.size())) {
        auto* navigation = new QWidget(d.tree.data());
        auto* layout = new QHBoxLayout(navigation);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        auto* backButton = new QToolButton(navigation);
        auto* forwardButton = new QToolButton(navigation);
        backButton->setIcon(d.iconLeft);
        forwardButton->setIcon(d.iconRight);
        backButton->setText(QString());
        forwardButton->setText(QString());
        backButton->setToolTip(QStringLiteral("Previous material node"));
        forwardButton->setToolTip(QStringLiteral("Next material node"));
        backButton->setEnabled(d.historyIndex > 0);
        forwardButton->setEnabled(d.historyIndex >= 0 && d.historyIndex + 1 < d.history.size());
        layout->addWidget(backButton);
        layout->addWidget(forwardButton);

        connect(backButton, &QToolButton::clicked, d.tree.data(), [this]() { back(); });
        connect(forwardButton, &QToolButton::clicked, d.tree.data(), [this]() { forward(); });
        d.tree->setItemWidget(nameItem, 2, navigation);
    }

    const QString type = node.typeLabel.isEmpty() ? QStringLiteral("UsdShade node") : node.typeLabel;
    addTextRow(QStringLiteral("Type"), type);

    QString description;
    if (node.shaderId == QStringLiteral("ND_standard_surface_surfaceshader"))
        description = QStringLiteral("MaterialX Standard Surface shader");
    else if (node.shaderId == QStringLiteral("UsdPreviewSurface"))
        description = QStringLiteral("USD Preview Surface shader");
    else if (!node.shaderId.isEmpty())
        description = QStringLiteral("Shader node: %1").arg(node.shaderId);
    else
        description = QStringLiteral("UsdShade connectable node");
    addTextRow(QStringLiteral("Description"), description);

    if (!node.shaderId.isEmpty())
        addTextRow(QStringLiteral("Shader ID"), node.shaderId);

    addTextRow(QStringLiteral("Path"), QString::fromStdString(node.path.GetString()),
               QString::fromStdString(node.path.GetString()));
}

static double
floatValue(const MaterialInputInfo& info, double fallback = 0.0)
{
    if (!info.hasValue)
        return fallback;
    if (info.value.IsHolding<float>())
        return info.value.UncheckedGet<float>();
    if (info.value.IsHolding<double>())
        return info.value.UncheckedGet<double>();
    return fallback;
}

static GfVec3f
colorValue(const MaterialInputInfo& info, const GfVec3f& fallback = GfVec3f(0.0f))
{
    if (!info.hasValue)
        return fallback;
    if (info.value.IsHolding<GfVec3f>())
        return info.value.UncheckedGet<GfVec3f>();
    return fallback;
}

static GfVec2f
vec2Value(const MaterialInputInfo& info, const GfVec2f& fallback = GfVec2f(0.0f))
{
    if (!info.hasValue)
        return fallback;
    if (info.value.IsHolding<GfVec2f>())
        return info.value.UncheckedGet<GfVec2f>();
    return fallback;
}

static GfVec4f
vec4Value(const MaterialInputInfo& info, const GfVec4f& fallback = GfVec4f(0.0f))
{
    if (!info.hasValue)
        return fallback;
    if (info.value.IsHolding<GfVec4f>())
        return info.value.UncheckedGet<GfVec4f>();
    return fallback;
}

namespace {
    constexpr double kUnboundedNumericLimit = 1.0e12;

    struct NumericEditorSpec {
        double minimum = -kUnboundedNumericLimit;
        double maximum = kUnboundedNumericLimit;
        double step = 0.01;
    };

    NumericEditorSpec numericEditorSpec(const MaterialInputInfo& info, bool colorComponent = false)
    {
        NumericEditorSpec spec;

        // QColor-backed color editing is normalized unless the shader definition
        // explicitly declares a different legal domain. Scalar/vector inputs are
        // otherwise treated as unbounded rather than silently clamped to 0..1.
        if (colorComponent && !info.hasUiMin && !info.hasUiMax) {
            spec.minimum = 0.0;
            spec.maximum = 1.0;
        }

        if (info.hasUiMin)
            spec.minimum = info.uiMin;
        if (info.hasUiMax)
            spec.maximum = info.uiMax;

        if (spec.minimum > spec.maximum)
            std::swap(spec.minimum, spec.maximum);

        if (info.hasUiStep && std::isfinite(info.uiStep) && info.uiStep > 0.0)
            spec.step = info.uiStep;
        else if (info.hasUiSoftMin && info.hasUiSoftMax && info.uiSoftMax > info.uiSoftMin)
            spec.step = std::max(1.0e-6, (info.uiSoftMax - info.uiSoftMin) / 100.0);

        return spec;
    }
}  // namespace

void
MaterialTreePrivate::addInputRow(QTreeWidgetItem* group, const MaterialInputInfo& info,
                                 const QList<MaterialInputInfo>& aggregate)
{
    if (!group)
        return;

    auto applyNextRowBackground = [this, group](QTreeWidgetItem* row) {
        if (!row)
            return;

        int& groupRow = d.groupPropertyRows[group];
        const bool alternate = (groupRow++ & 1) == 0;
        const QColor rowColor = style()->color(alternate ? Style::ColorRole::ItemAlt : Style::ColorRole::Item);
        const QBrush rowBrush(rowColor);
        for (int column = 0; column < d.tree->columnCount(); ++column)
            row->setBackground(column, rowBrush);
    };

    auto prepareRowWidget = [](QWidget* widget) {
        if (!widget)
            return;
        widget->setAutoFillBackground(false);
        widget->setAttribute(Qt::WA_TranslucentBackground, true);
    };

    auto* item = new QTreeWidgetItem(group);
    item->setText(0, info.label);
    item->setData(0, Qt::UserRole, QString::fromStdString(info.inputPath.GetString()));
    item->setToolTip(0, QString::fromStdString(info.inputPath.GetString()));
    applyNextRowBackground(item);

    auto setActionButton = [this](QTreeWidgetItem* row, Style::IconRole, const QString& tooltip, const SdfPath& path) {
        if (!row)
            return;
        row->setIcon(2, d.iconRight);
        row->setToolTip(2, tooltip);
        row->setTextAlignment(2, Qt::AlignCenter);
        row->setData(2, kActionRole, OpenNodeAction);
        row->setData(2, kActionPathRole, QString::fromStdString(path.GetString()));
    };

    if (info.connected) {
        QString source = QString::fromStdString(info.sourcePrimPath.GetName());
        if (!info.sourceName.IsEmpty())
            source += QString(".%1").arg(QString::fromStdString(info.sourceName.GetString()));
        if (source.isEmpty())
            source = aggregate.size() > 1 ? QStringLiteral("Mixed connections") : QStringLiteral("Connected");

        item->setText(1, source);
        item->setToolTip(1, QString::fromStdString(info.sourcePrimPath.GetString()));

        if (d.materials.size() == 1 && !info.sourcePrimPath.IsEmpty())
            setActionButton(item, Style::IconRole::Right, QStringLiteral("Open connected node"), info.sourcePrimPath);
        return;
    }

    auto addNodeButton = [this, item, info]() {
        if (d.materials.size() != 1 || info.inputPath.IsEmpty())
            return;
        item->setIcon(2, d.iconNew);
        item->setToolTip(2, QStringLiteral("Connect shader node"));
        item->setTextAlignment(2, Qt::AlignCenter);
        item->setData(2, kActionRole, AddNodeAction);
    };

    if (info.isFloat()) {
        const NumericEditorSpec spec = numericEditorSpec(info);
        auto* control = new FloatControl(spec.minimum, spec.maximum, spec.step, d.tree.data());
        prepareRowWidget(control);
        double total = 0.0;
        bool mixed = false;
        const double first = aggregate.isEmpty() ? floatValue(info) : floatValue(aggregate.first());
        for (const MaterialInputInfo& value : aggregate) {
            const double v = floatValue(value);
            total += v;
            if (std::abs(v - first) > 1e-6)
                mixed = true;
        }
        control->setValue(aggregate.isEmpty() ? floatValue(info) : total / aggregate.size(), mixed);
        QList<SdfPath> editPaths;
        for (const MaterialInputInfo& value : aggregate) {
            if (!value.inputPath.IsEmpty() && !value.connected)
                editPaths.append(value.inputPath);
        }
        control->previewChanged = [this, editPaths](double value) {
            if (!editPaths.isEmpty())
                Q_EMIT d.tree->floatInputsPreviewChanged(editPaths, value);
        };
        control->changed = [this, editPaths](double value) {
            if (!editPaths.isEmpty())
                Q_EMIT d.tree->floatInputsChanged(editPaths, value);
        };
        d.tree->setItemWidget(item, 1, control);
        control->show();
        item->setSizeHint(1, QSize(0, 30));
        addNodeButton();
        return;
    }

    if (info.isColor3()) {
        item->setExpanded(true);
        addNodeButton();

        QList<SdfPath> editPaths;
        for (const MaterialInputInfo& value : aggregate) {
            if (!value.inputPath.IsEmpty() && !value.connected)
                editPaths.append(value.inputPath);
        }

        const char* suffixes[] = { "R", "G", "B" };
        for (int channel = 0; channel < 3; ++channel) {
            auto* channelItem = new QTreeWidgetItem(item);
            channelItem->setText(0, QString::fromLatin1(suffixes[channel]));
            channelItem->setData(0, Qt::UserRole, QString::fromStdString(info.inputPath.GetString()));
            channelItem->setToolTip(0, QString::fromStdString(info.inputPath.GetString()));
            applyNextRowBackground(channelItem);

            const NumericEditorSpec spec = numericEditorSpec(info, true);
            auto* control = new FloatControl(spec.minimum, spec.maximum, spec.step, d.tree.data());
            prepareRowWidget(control);

            double total = 0.0;
            bool mixed = false;
            const float first = aggregate.isEmpty() ? colorValue(info)[channel]
                                                    : colorValue(aggregate.first())[channel];
            for (const MaterialInputInfo& value : aggregate) {
                const float v = colorValue(value)[channel];
                total += v;
                if (std::abs(v - first) > 1e-6)
                    mixed = true;
            }

            control->setValue(aggregate.isEmpty() ? first : total / aggregate.size(), mixed);
            const QString key = QString("%1:%2").arg(info.parameter).arg(channel);
            d.colorControls.insert(key, control);

            control->previewChanged = [this, parameter = info.parameter, editPaths](double) {
                const QColor color = currentColor(parameter);
                if (color.isValid() && !editPaths.isEmpty())
                    Q_EMIT d.tree->colorInputsPreviewChanged(editPaths, color);
            };
            control->changed = [this, parameter = info.parameter, editPaths](double) {
                const QColor color = currentColor(parameter);
                if (color.isValid() && !editPaths.isEmpty())
                    Q_EMIT d.tree->colorInputsChanged(editPaths, color);
            };
            d.tree->setItemWidget(channelItem, 1, control);
            control->show();
            channelItem->setSizeHint(1, QSize(0, 30));
        }
        return;
    }

    QList<SdfPath> editPaths;
    for (const MaterialInputInfo& value : aggregate) {
        if (!value.inputPath.IsEmpty() && !value.connected)
            editPaths.append(value.inputPath);
    }

    if (info.typeName == SdfValueTypeNames->Asset) {
        const SdfAssetPath asset = info.hasValue && info.value.IsHolding<SdfAssetPath>()
                                       ? info.value.UncheckedGet<SdfAssetPath>()
                                       : SdfAssetPath();
        auto* container = new QWidget(d.tree.data());
        prepareRowWidget(container);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 4, 0);
        layout->setSpacing(4);

        auto* edit = new QLineEdit(container);
        edit->setReadOnly(true);
        edit->setFrame(false);
        layout->addWidget(edit, 1);

        auto* browse = new QToolButton(container);
        browse->setIcon(d.iconOpen);
        browse->setText(QString());
        browse->setToolTip(QStringLiteral("Choose file"));
        browse->setAutoRaise(true);
        browse->setFixedSize(28, 28);
        layout->addWidget(browse);

        connect(browse, &QToolButton::clicked, d.tree.data(), [this, editPaths, edit]() {
            if (editPaths.isEmpty())
                return;
            const QString current = edit->text().trimmed();
            const QString filename = QFileDialog::getOpenFileName(d.tree.data(), tr("Choose texture or asset"),
                                                                  current);
            if (filename.isEmpty())
                return;
            edit->setText(filename);
            edit->setToolTip(filename);
            setInputValues(editPaths, VtValue(SdfAssetPath(filename.toStdString())));
        });

        if (edit) {
            edit->setText(QString::fromStdString(asset.GetAssetPath()));
            edit->setToolTip(QString::fromStdString(asset.GetAssetPath()));
        }
        d.tree->setItemWidget(item, 1, container);
        container->show();
        item->setSizeHint(1, QSize(0, 30));
        addNodeButton();
        return;
    }

    if (!info.options.isEmpty()
        && (info.typeName == SdfValueTypeNames->String || info.typeName == SdfValueTypeNames->Token)) {
        auto* combo = new QComboBox(d.tree.data());
        prepareRowWidget(combo);
        combo->blockSignals(true);
        combo->clear();
        combo->addItems(info.options);

        QString current;
        if (info.hasValue && info.value.IsHolding<std::string>())
            current = QString::fromStdString(info.value.UncheckedGet<std::string>());
        else if (info.hasValue && info.value.IsHolding<TfToken>())
            current = QString::fromStdString(info.value.UncheckedGet<TfToken>().GetString());

        const int currentIndex = combo->findText(current);
        if (currentIndex >= 0)
            combo->setCurrentIndex(currentIndex);
        else if (!current.isEmpty()) {
            combo->insertItem(0, current);
            combo->setCurrentIndex(0);
        }

        combo->blockSignals(false);
        const bool tokenType = info.typeName == SdfValueTypeNames->Token;
        connect(combo, qOverload<int>(&QComboBox::activated), d.tree.data(), [this, editPaths, combo, tokenType](int) {
            if (editPaths.isEmpty())
                return;
            const QString text = combo->currentText();
            if (tokenType)
                setInputValues(editPaths, VtValue(TfToken(text.toStdString())));
            else
                setInputValues(editPaths, VtValue(text.toStdString()));
        });
        d.tree->setItemWidget(item, 1, combo);
        combo->show();
        item->setSizeHint(1, QSize(0, 30));
        addNodeButton();
        return;
    }

    if (info.typeName == SdfValueTypeNames->String || info.typeName == SdfValueTypeNames->Token) {
        auto* edit = new QLineEdit(d.tree.data());
        prepareRowWidget(edit);
        edit->clear();
        if (info.hasValue && info.value.IsHolding<std::string>())
            edit->setText(QString::fromStdString(info.value.UncheckedGet<std::string>()));
        else if (info.hasValue && info.value.IsHolding<TfToken>())
            edit->setText(QString::fromStdString(info.value.UncheckedGet<TfToken>().GetString()));

        const bool tokenType = info.typeName == SdfValueTypeNames->Token;
        connect(edit, &QLineEdit::editingFinished, d.tree.data(), [this, editPaths, edit, tokenType]() {
            if (editPaths.isEmpty())
                return;
            const QString text = edit->text();
            if (tokenType)
                setInputValues(editPaths, VtValue(TfToken(text.trimmed().toStdString())));
            else
                setInputValues(editPaths, VtValue(text.toStdString()));
        });
        d.tree->setItemWidget(item, 1, edit);
        edit->show();
        item->setSizeHint(1, QSize(0, 30));
        addNodeButton();
        return;
    }

    if (info.typeName == SdfValueTypeNames->Bool) {
        auto* combo = new QComboBox(d.tree.data());
        combo->addItem(QStringLiteral("false"), false);
        combo->addItem(QStringLiteral("true"), true);
        prepareRowWidget(combo);
        combo->setCurrentIndex(info.hasValue && info.value.IsHolding<bool>() && info.value.UncheckedGet<bool>() ? 1
                                                                                                                : 0);
        connect(combo, qOverload<int>(&QComboBox::activated), d.tree.data(), [this, editPaths, combo](int) {
            if (!editPaths.isEmpty())
                setInputValues(editPaths, VtValue(combo->currentData().toBool()));
        });
        d.tree->setItemWidget(item, 1, combo);
        combo->show();
        item->setSizeHint(1, QSize(0, 30));
        addNodeButton();
        return;
    }

    if (info.typeName == SdfValueTypeNames->Int) {
        auto* spin = new QSpinBox(d.tree.data());
        int minimum = std::numeric_limits<int>::min();
        int maximum = std::numeric_limits<int>::max();
        if (info.hasUiMin)
            minimum = static_cast<int>(std::clamp(std::ceil(info.uiMin),
                                                  static_cast<double>(std::numeric_limits<int>::min()),
                                                  static_cast<double>(std::numeric_limits<int>::max())));
        if (info.hasUiMax)
            maximum = static_cast<int>(std::clamp(std::floor(info.uiMax),
                                                  static_cast<double>(std::numeric_limits<int>::min()),
                                                  static_cast<double>(std::numeric_limits<int>::max())));
        if (minimum > maximum)
            std::swap(minimum, maximum);
        spin->setRange(minimum, maximum);
        if (info.hasUiStep && std::isfinite(info.uiStep) && info.uiStep > 0.0)
            spin->setSingleStep(std::max(1, static_cast<int>(std::round(info.uiStep))));
        prepareRowWidget(spin);
        spin->setValue(info.hasValue && info.value.IsHolding<int>() ? info.value.UncheckedGet<int>() : 0);
        connect(spin, &QSpinBox::editingFinished, d.tree.data(), [this, editPaths, spin]() {
            if (!editPaths.isEmpty())
                setInputValues(editPaths, VtValue(spin->value()));
        });
        d.tree->setItemWidget(item, 1, spin);
        spin->show();
        item->setSizeHint(1, QSize(0, 30));
        addNodeButton();
        return;
    }

    if (info.typeName == SdfValueTypeNames->Float2 || info.typeName == SdfValueTypeNames->TexCoord2f) {
        item->setExpanded(true);
        addNodeButton();

        const GfVec2f initial = vec2Value(info);
        const char* suffixes[] = { "X", "Y" };
        auto controls = std::make_shared<std::array<QPointer<SpinBox>, 2>>();

        for (int channel = 0; channel < 2; ++channel) {
            auto* channelItem = new QTreeWidgetItem(item);
            channelItem->setText(0, QString::fromLatin1(suffixes[channel]));
            channelItem->setData(0, Qt::UserRole, QString::fromStdString(info.inputPath.GetString()));
            applyNextRowBackground(channelItem);

            auto* spin = new SpinBox(d.tree.data());
            const NumericEditorSpec spec = numericEditorSpec(info);
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(4);
            spin->setSingleStep(spec.step);
            prepareRowWidget(spin);
            spin->setValue(initial[channel]);
            (*controls)[channel] = spin;

            connect(spin, &QDoubleSpinBox::editingFinished, d.tree.data(), [this, editPaths, controls]() {
                if (editPaths.isEmpty() || !(*controls)[0] || !(*controls)[1])
                    return;

                const GfVec2f value(static_cast<float>((*controls)[0]->value()),
                                    static_cast<float>((*controls)[1]->value()));
                setInputValues(editPaths, VtValue(value));
            });
            d.tree->setItemWidget(channelItem, 1, spin);
            spin->show();
            channelItem->setSizeHint(1, QSize(0, 30));
        }
        return;
    }

    if (info.typeName == SdfValueTypeNames->Float3) {
        item->setExpanded(true);
        addNodeButton();

        const GfVec3f initial = colorValue(info);
        const char* suffixes[] = { "X", "Y", "Z" };
        auto controls = std::make_shared<std::array<QPointer<SpinBox>, 3>>();

        for (int channel = 0; channel < 3; ++channel) {
            auto* channelItem = new QTreeWidgetItem(item);
            channelItem->setText(0, QString::fromLatin1(suffixes[channel]));
            channelItem->setData(0, Qt::UserRole, QString::fromStdString(info.inputPath.GetString()));
            channelItem->setToolTip(0, QString::fromStdString(info.inputPath.GetString()));
            applyNextRowBackground(channelItem);

            auto* spin = new SpinBox(d.tree.data());
            const NumericEditorSpec spec = numericEditorSpec(info);
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(4);
            spin->setSingleStep(spec.step);
            prepareRowWidget(spin);
            spin->setValue(initial[channel]);
            (*controls)[channel] = spin;

            connect(spin, &QDoubleSpinBox::editingFinished, d.tree.data(), [this, editPaths, controls]() {
                if (editPaths.isEmpty() || !(*controls)[0] || !(*controls)[1] || !(*controls)[2])
                    return;

                const GfVec3f value(static_cast<float>((*controls)[0]->value()),
                                    static_cast<float>((*controls)[1]->value()),
                                    static_cast<float>((*controls)[2]->value()));
                setInputValues(editPaths, VtValue(value));
            });
            d.tree->setItemWidget(channelItem, 1, spin);
            spin->show();
            channelItem->setSizeHint(1, QSize(0, 30));
        }
        return;
    }

    if (info.typeName == SdfValueTypeNames->Float4 || info.typeName == SdfValueTypeNames->Color4f) {
        item->setExpanded(true);
        addNodeButton();

        const GfVec4f initial = vec4Value(info);
        const bool color = info.typeName == SdfValueTypeNames->Color4f;
        const char* vectorSuffixes[] = { "X", "Y", "Z", "W" };
        const char* colorSuffixes[] = { "R", "G", "B", "A" };
        auto controls = std::make_shared<std::array<QPointer<SpinBox>, 4>>();

        for (int channel = 0; channel < 4; ++channel) {
            auto* channelItem = new QTreeWidgetItem(item);
            channelItem->setText(0, QString::fromLatin1(color ? colorSuffixes[channel] : vectorSuffixes[channel]));
            channelItem->setData(0, Qt::UserRole, QString::fromStdString(info.inputPath.GetString()));
            channelItem->setToolTip(0, QString::fromStdString(info.inputPath.GetString()));
            applyNextRowBackground(channelItem);

            auto* spin = new SpinBox(d.tree.data());
            const NumericEditorSpec spec = numericEditorSpec(info, color);
            spin->setRange(spec.minimum, spec.maximum);
            spin->setDecimals(4);
            spin->setSingleStep(spec.step);
            prepareRowWidget(spin);
            spin->setValue(initial[channel]);
            (*controls)[channel] = spin;

            connect(spin, &QDoubleSpinBox::editingFinished, d.tree.data(), [this, editPaths, controls]() {
                if (editPaths.isEmpty() || !(*controls)[0] || !(*controls)[1] || !(*controls)[2] || !(*controls)[3])
                    return;

                const GfVec4f value(static_cast<float>((*controls)[0]->value()),
                                    static_cast<float>((*controls)[1]->value()),
                                    static_cast<float>((*controls)[2]->value()),
                                    static_cast<float>((*controls)[3]->value()));
                setInputValues(editPaths, VtValue(value));
            });
            d.tree->setItemWidget(channelItem, 1, spin);
            spin->show();
            channelItem->setSizeHint(1, QSize(0, 30));
        }
        return;
    }

    QString valueText;
    if (info.hasValue)
        valueText = QString::fromStdString(TfStringify(info.value));
    else
        valueText = QString("<%1>").arg(QString::fromStdString(info.typeName.GetAsToken().GetString()));

    item->setText(1, valueText);
    item->setToolTip(1, valueText);
    addNodeButton();
}


void
MaterialTreePrivate::setInputValues(const QList<SdfPath>& inputPaths, const VtValue& value)
{
    if (inputPaths.isEmpty() || value.IsEmpty())
        return;

    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;
        for (const SdfPath& path : inputPaths) {
            if (!stage->GetAttributeAtPath(path)) {
                if (!MaterialUtils::ensureShaderInput(stage, path))
                    return;
            }
        }
    }

    session()->commandStack()->run(new Command(setAttributeValues(inputPaths, value)));
}

QColor
MaterialTreePrivate::currentColor(const QString& parameter) const
{
    FloatControl* r = d.colorControls.value(parameter + ":0");
    FloatControl* g = d.colorControls.value(parameter + ":1");
    FloatControl* b = d.colorControls.value(parameter + ":2");
    if (!r || !g || !b)
        return {};
    return QColor::fromRgbF(r->value(), g->value(), b->value());
}

bool
MaterialTreePrivate::sameNodeStructure(const MaterialNodeInfo& a, const MaterialNodeInfo& b) const
{
    if (a.path != b.path || a.shaderId != b.shaderId || a.inputs.size() != b.inputs.size())
        return false;

    for (int i = 0; i < a.inputs.size(); ++i) {
        const MaterialInputInfo& lhs = a.inputs[i];
        const MaterialInputInfo& rhs = b.inputs[i];
        if (lhs.inputPath != rhs.inputPath || lhs.inputName != rhs.inputName || lhs.typeName != rhs.typeName
            || lhs.label != rhs.label || lhs.group != rhs.group || lhs.connected != rhs.connected) {
            return false;
        }
        if (lhs.connected
            && (lhs.sourcePrimPath != rhs.sourcePrimPath || lhs.sourceName != rhs.sourceName
                || lhs.sourceShaderId != rhs.sourceShaderId)) {
            return false;
        }
    }
    return true;
}

QTreeWidgetItem*
MaterialTreePrivate::inputItem(const SdfPath& path) const
{
    if (!d.tree || path.IsEmpty())
        return nullptr;

    const QString wanted = QString::fromStdString(path.GetString());
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* item) -> QTreeWidgetItem* {
        if (!item)
            return nullptr;
        if (item->data(0, Qt::UserRole).toString() == wanted)
            return item;
        for (int i = 0; i < item->childCount(); ++i) {
            if (QTreeWidgetItem* found = find(item->child(i)))
                return found;
        }
        return nullptr;
    };

    for (int i = 0; i < d.tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found = find(d.tree->topLevelItem(i)))
            return found;
    }
    return nullptr;
}

void
MaterialTreePrivate::refreshInputItem(QTreeWidgetItem* item, const MaterialInputInfo& info)
{
    if (!item || !d.tree || info.connected)
        return;

    // FloatControl::setValue() has its own signal guard, so these updates do not
    // feed back into USD or schedule another interactive preview.
    if (info.isFloat()) {
        if (auto* control = dynamic_cast<FloatControl*>(d.tree->itemWidget(item, 1)))
            control->setValue(floatValue(info), false);
        return;
    }

    if (info.isColor3()) {
        const GfVec3f value = colorValue(info);
        for (int channel = 0; channel < std::min(3, item->childCount()); ++channel) {
            if (auto* control = dynamic_cast<FloatControl*>(d.tree->itemWidget(item->child(channel), 1)))
                control->setValue(value[channel], false);
        }
        return;
    }

    QWidget* editor = d.tree->itemWidget(item, 1);

    if (info.typeName == SdfValueTypeNames->Asset) {
        QLineEdit* edit = editor ? editor->findChild<QLineEdit*>() : nullptr;
        if (!edit)
            return;
        const SdfAssetPath asset = info.hasValue && info.value.IsHolding<SdfAssetPath>()
                                       ? info.value.UncheckedGet<SdfAssetPath>()
                                       : SdfAssetPath();
        const QSignalBlocker blocker(edit);
        edit->setText(QString::fromStdString(asset.GetAssetPath()));
        return;
    }

    if (!info.options.isEmpty()
        && (info.typeName == SdfValueTypeNames->String || info.typeName == SdfValueTypeNames->Token)) {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo)
            return;
        QString value;
        if (info.hasValue && info.value.IsHolding<std::string>())
            value = QString::fromStdString(info.value.UncheckedGet<std::string>());
        else if (info.hasValue && info.value.IsHolding<TfToken>())
            value = QString::fromStdString(info.value.UncheckedGet<TfToken>().GetString());
        const QSignalBlocker blocker(combo);
        int index = combo->findText(value);
        if (index < 0 && !value.isEmpty()) {
            combo->insertItem(0, value);
            index = 0;
        }
        if (index >= 0)
            combo->setCurrentIndex(index);
        return;
    }

    if (info.typeName == SdfValueTypeNames->String || info.typeName == SdfValueTypeNames->Token) {
        auto* edit = qobject_cast<QLineEdit*>(editor);
        if (!edit)
            return;
        QString value;
        if (info.hasValue && info.value.IsHolding<std::string>())
            value = QString::fromStdString(info.value.UncheckedGet<std::string>());
        else if (info.hasValue && info.value.IsHolding<TfToken>())
            value = QString::fromStdString(info.value.UncheckedGet<TfToken>().GetString());
        const QSignalBlocker blocker(edit);
        edit->setText(value);
        return;
    }

    if (info.typeName == SdfValueTypeNames->Bool) {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo)
            return;
        const bool value = info.hasValue && info.value.IsHolding<bool>() && info.value.UncheckedGet<bool>();
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(value ? 1 : 0);
        return;
    }

    if (info.typeName == SdfValueTypeNames->Int) {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (!spin)
            return;
        const int value = info.hasValue && info.value.IsHolding<int>() ? info.value.UncheckedGet<int>() : 0;
        const QSignalBlocker blocker(spin);
        spin->setValue(value);
        return;
    }

    if (info.typeName == SdfValueTypeNames->Float2 || info.typeName == SdfValueTypeNames->TexCoord2f) {
        const GfVec2f value = vec2Value(info);
        for (int channel = 0; channel < std::min(2, item->childCount()); ++channel) {
            auto* spin = qobject_cast<SpinBox*>(d.tree->itemWidget(item->child(channel), 1));
            if (!spin)
                continue;
            const QSignalBlocker blocker(spin);
            spin->setValue(value[channel]);
        }
        return;
    }

    if (info.typeName == SdfValueTypeNames->Float3) {
        const GfVec3f value = colorValue(info);
        for (int channel = 0; channel < std::min(3, item->childCount()); ++channel) {
            auto* spin = qobject_cast<SpinBox*>(d.tree->itemWidget(item->child(channel), 1));
            if (!spin)
                continue;
            const QSignalBlocker blocker(spin);
            spin->setValue(value[channel]);
        }
        return;
    }

    if (info.typeName == SdfValueTypeNames->Float4 || info.typeName == SdfValueTypeNames->Color4f) {
        const GfVec4f value = vec4Value(info);
        for (int channel = 0; channel < std::min(4, item->childCount()); ++channel) {
            auto* spin = qobject_cast<SpinBox*>(d.tree->itemWidget(item->child(channel), 1));
            if (!spin)
                continue;
            const QSignalBlocker blocker(spin);
            spin->setValue(value[channel]);
        }
        return;
    }

    const QString valueText = info.hasValue
                                  ? QString::fromStdString(TfStringify(info.value))
                                  : QString("<%1>").arg(QString::fromStdString(info.typeName.GetAsToken().GetString()));
    item->setText(1, valueText);
    item->setToolTip(1, valueText);
}

bool
MaterialTreePrivate::refreshSingleValues()
{
    if (d.materials.size() != 1 || d.currentNode.IsEmpty() || d.displayedNode.path.IsEmpty())
        return false;
    MaterialNodeInfo node;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return false;
        node = MaterialUtils::nodeInfo(stage, d.currentNode);
    }

    if (node.path.IsEmpty() || !sameNodeStructure(d.displayedNode, node))
        return false;

    for (const MaterialInputInfo& info : node.inputs) {
        if (QTreeWidgetItem* item = inputItem(info.inputPath))
            refreshInputItem(item, info);
    }

    d.displayedNode = node;
    return true;
}

void
MaterialTreePrivate::captureExpansionState()
{
    if (d.materials.size() != 1 || d.displayedNode.path.IsEmpty() || !d.tree)
        return;

    const QString nodeKey = QString::fromStdString(d.displayedNode.path.GetString());
    QSet<QString> expanded;
    bool foundGroup = false;
    for (int i = 0; i < d.tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = d.tree->topLevelItem(i);
        if (!item)
            continue;
        if (!item->data(0, kGroupPopulatedRole).isValid())
            continue;
        const QString groupName = item->data(0, kGroupNameRole).toString();
        foundGroup = true;
        if (item->isExpanded())
            expanded.insert(groupName);
    }
    if (!foundGroup)
        return;

    d.nodesWithExpansionState.insert(nodeKey);
    d.expandedGroupsByNode.insert(nodeKey, expanded);
}

void
MaterialTreePrivate::populateGroup(QTreeWidgetItem* group)
{
    if (!group || group->data(0, kGroupPopulatedRole).toBool())
        return;

    if (!group->data(0, kGroupPopulatedRole).isValid())
        return;

    const QString groupName = group->data(0, kGroupNameRole).toString();
    group->setData(0, kGroupPopulatedRole, true);
    group->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
    d.groupPropertyRows.remove(group);

    for (const MaterialInputInfo& info : d.displayedNode.inputs) {
        if (info.group != groupName)
            continue;
        addInputRow(group, info, { info });
    }
}

void
MaterialTreePrivate::rebuildSingle()
{
    if (d.materials.size() != 1)
        return;

    MaterialNodeInfo node;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        const MaterialEntry& material = d.materials.first();
        if (d.currentNode.IsEmpty())
            d.currentNode = material.shaderPath;

        node = MaterialUtils::nodeInfo(stage, d.currentNode);
    }

    if (node.path.IsEmpty())
        return;

    d.displayedNode = node;

    Q_EMIT d.tree->currentNodeChanged(node.path, node.name,
                                      node.typeLabel.isEmpty() ? QStringLiteral("UsdShade node") : node.typeLabel,
                                      node.shaderId);

    const QString nodeKey = QString::fromStdString(node.path.GetString());
    const bool hasExpansionState = d.nodesWithExpansionState.contains(nodeKey);
    const QSet<QString> expandedGroups = d.expandedGroupsByNode.value(nodeKey);

    QHash<QString, QTreeWidgetItem*> groups;
    QString firstGroup;
    bool firstGroupSet = false;
    for (const MaterialInputInfo& info : node.inputs) {
        if (groups.contains(info.group))
            continue;

        auto* group = new QTreeWidgetItem(d.tree.data());
        group->setText(0, info.group);
        group->setFirstColumnSpanned(true);
        group->setData(0, kGroupNameRole, info.group);
        group->setData(0, kGroupPopulatedRole, false);
        group->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        QFont font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);
        groups.insert(info.group, group);

        if (!firstGroupSet) {
            firstGroup = info.group;
            firstGroupSet = true;
        }

        const bool expanded = hasExpansionState ? expandedGroups.contains(info.group) : info.group == firstGroup;
        group->setExpanded(expanded);
        if (expanded) {
            populateGroup(group);
        }
    }
}

void
MaterialTreePrivate::rebuildMulti()
{
    if (d.materials.size() < 2)
        return;

    // Multi-edit deliberately exposes the intersection only. This keeps the
    // displayed value and authored operation truthful for every selection.
    QStringList keys = d.materials.first().inputs.keys();
    for (int i = 1; i < d.materials.size(); ++i) {
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [&](const QString& key) {
                                      return !d.materials[i].inputs.contains(key)
                                             || d.materials[i].inputs.value(key).typeName
                                                    != d.materials.first().inputs.value(key).typeName;
                                  }),
                   keys.end());
    }

    const QStringList canonicalOrder
        = { "baseColor", "metalness", "roughness",     "opacity",      "specular",
            "ior",       "coat",      "coatRoughness", "transmission", "transmissionColor" };
    std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
        return canonicalOrder.indexOf(a) < canonicalOrder.indexOf(b);
    });

    QHash<QString, QTreeWidgetItem*> groups;
    for (const QString& key : keys) {
        QList<MaterialInputInfo> values;
        bool anyConnected = false;
        for (const MaterialEntry& material : d.materials) {
            const MaterialInputInfo info = material.inputs.value(key);
            values.append(info);
            anyConnected = anyConnected || info.connected;
        }

        MaterialInputInfo representative = values.first();
        // If any selected material is connected, do not present a numeric editor
        // that would only affect a subset of the selection.
        representative.connected = anyConnected;
        if (anyConnected) {
            representative.sourcePrimPath = {};
            representative.sourceName = TfToken();
        }

        QTreeWidgetItem* group = groups.value(representative.group);
        if (!group) {
            group = addGroup(d.tree.data(), representative.group);
            groups.insert(representative.group, group);
        }
        addInputRow(group, representative, values);
    }
}

void
MaterialTreePrivate::rebuild(bool preserveScroll)
{
    captureExpansionState();
    // Attribute notices can rebuild this inspector many times while a control is
    // being edited. QTreeWidget::clear() resets both scroll bars to zero, which
    // made the property view jump back to the first MaterialX category on every
    // value change. Preserve the viewport only when we are refreshing the same
    // material/node; explicit navigation deliberately starts with a fresh view.
    const int horizontalScroll = preserveScroll && d.tree->horizontalScrollBar()
                                     ? d.tree->horizontalScrollBar()->value()
                                     : 0;
    const int verticalScroll = preserveScroll && d.tree->verticalScrollBar() ? d.tree->verticalScrollBar()->value() : 0;
    // materialtree.ui is translated after the widget constructor and can
    // restore its Designer-time single-column header. Keep the runtime
    // inspector contract authoritative whenever the tree is rebuilt.
    d.tree->setUpdatesEnabled(false);
    const QSignalBlocker treeSignals(d.tree.data());
    d.tree->setColumnCount(3);
    d.tree->setHeaderLabels({ "Property", "Value", "" });
    // QTreeWidget owns widgets installed with setItemWidget(). Do not detach/reuse
    // them across clear(); QTreeView keeps internal index-widget bookkeeping until layout.
    d.tree->clear();
    d.colorControls.clear();
    d.displayedNode = MaterialNodeInfo();
    d.groupPropertyRows.clear();
    if (d.materials.isEmpty()) {
        d.tree->setEnabled(false);
        d.tree->setUpdatesEnabled(true);
        d.tree->viewport()->update();
        return;
    }
    d.tree->setEnabled(true);
    if (d.materials.size() == 1)
        rebuildSingle();
    else {
        rebuildMulti();
        d.tree->expandAll();
    }
    d.tree->setUpdatesEnabled(true);
    d.tree->viewport()->update();


    if (preserveScroll) {
        // Restoring immediately can still be clamped against the old scrollbar
        // range. Defer until QTreeWidget has recalculated the
        // row geometry/ranges, then restore the exact viewport position.
        QPointer<MaterialTree> tree = d.tree;
        QTimer::singleShot(0, d.tree.data(), [tree, horizontalScroll, verticalScroll]() {
            if (!tree)
                return;
            if (tree->horizontalScrollBar())
                tree->horizontalScrollBar()->setValue(horizontalScroll);
            if (tree->verticalScrollBar())
                tree->verticalScrollBar()->setValue(verticalScroll);
        });
    }
}

void
MaterialTreePrivate::navigateTo(const SdfPath& path, bool pushHistory)
{
    if (path.IsEmpty() || d.materials.size() != 1)
        return;

    if (path == d.currentNode && d.displayedNode.path == path)
        return;

    if (pushHistory) {
        if (d.historyIndex + 1 < d.history.size())
            d.history.resize(d.historyIndex + 1);
        if (d.history.isEmpty() || d.history.last() != path) {
            d.history.append(path);
            d.historyIndex = static_cast<int>(d.history.size() - 1);
        }
    }
    d.currentNode = path;
    rebuild();
}

void
MaterialTreePrivate::back()
{
    if (d.historyIndex <= 0)
        return;
    --d.historyIndex;
    d.currentNode = d.history[d.historyIndex];
    rebuild();
}

void
MaterialTreePrivate::forward()
{
    if (d.historyIndex < 0 || d.historyIndex + 1 >= d.history.size())
        return;
    ++d.historyIndex;
    d.currentNode = d.history[d.historyIndex];
    rebuild();
}

void
MaterialTreePrivate::showAddNodeMenu(const MaterialInputInfo& info, QWidget* anchor)
{
    if (!anchor || info.inputPath.IsEmpty())
        return;

    QMenu menu(anchor);

    MaterialMenu::Request request;
    request.targetType = info.typeName;

    // Match MaterialGraph exactly: keep node creation within the shading family
    // of the material being edited, and flatten that family directly into this
    // slot menu. This avoids the legacy mixed USD Preview + MaterialX menu.
    const bool materialX = d.materials.size() == 1 && d.materials.first().shaderId.startsWith(QStringLiteral("ND_"));
    request.materialXFirst = materialX;
    request.includeSearch = true;
    request.materialXOnly = materialX;
    request.usdPreviewOnly = !materialX;
    request.flattenFamily = true;

    MaterialMenu::populate(&menu, request, d.tree.data(), [this, info](const MaterialMenu::Choice& choice) {
        if (choice.family == MaterialMenu::Family::MaterialX) {
            Q_EMIT d.tree->connectMaterialXNodeRequested(info.inputPath, choice.nodeDef, choice.nodeName);
        }
        else {
            Q_EMIT d.tree->connectShaderNodeRequested(info.inputPath, choice.shaderId, choice.nodeName,
                                                      choice.outputName);
        }
    });

    menu.exec(QCursor::pos());
}

void
MaterialTreePrivate::showInputMenu(const QPoint& position)
{
    QTreeWidgetItem* item = d.tree->itemAt(position);
    if (!item)
        return;

    const SdfPath inputPath(item->data(0, Qt::UserRole).toString().toStdString());
    if (inputPath.IsEmpty() || !inputPath.IsPropertyPath())
        return;

    UsdStageRefPtr stage;
    MaterialInputInfo info;
    QStringList network;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        stage = session()->stageUnsafe();
        if (!stage)
            return;
        const MaterialNodeInfo node = MaterialUtils::nodeInfo(stage, inputPath.GetPrimPath());
        for (const MaterialInputInfo& candidate : node.inputs) {
            if (candidate.inputPath == inputPath) {
                info = candidate;
                break;
            }
        }
        network = MaterialUtils::networkDescription(stage, inputPath, 4);
    }

    QMenu menu(d.tree.data());
    if (info.connected) {
        QMenu* networkMenu = menu.addMenu(QStringLiteral("Network"));
        if (network.isEmpty()) {
            QAction* empty = networkMenu->addAction(QStringLiteral("Connected"));
            empty->setEnabled(false);
        }
        else {
            for (const QString& line : network) {
                QAction* text = networkMenu->addAction(line);
                text->setEnabled(false);
            }
        }
        if (d.materials.size() == 1 && !info.sourcePrimPath.IsEmpty()) {
            QAction* go = menu.addAction(QStringLiteral("Go to source"));
            connect(go, &QAction::triggered, d.tree.data(), [this, path = info.sourcePrimPath]() { navigateTo(path); });
        }
        QAction* copy = menu.addAction(QStringLiteral("Copy source path"));
        connect(copy, &QAction::triggered, d.tree.data(), [path = info.sourcePrimPath]() {
            QApplication::clipboard()->setText(QString::fromStdString(path.GetString()));
        });
        menu.addSeparator();
    }

    // Keep the two operations explicit and independent. Disconnect only breaks
    // an incoming connection; Reset Value clears the authored value while
    // preserving the shader input property itself.
    QAction* disconnect = menu.addAction(QStringLiteral("Disconnect"));
    disconnect->setEnabled(info.connected);
    connect(disconnect, &QAction::triggered, d.tree.data(),
            [this, inputPath]() { Q_EMIT d.tree->disconnectInputsRequested({ inputPath }); });

    QAction* reset = menu.addAction(QStringLiteral("Reset Value"));
    // For NodeDef-declared inputs, Reset means reset to the NodeDef default,
    // even when the currently composed value originates in an imported/weaker
    // layer. Clearing only the edit-layer opinion would simply reveal that same
    // imported value again. For custom/non-NodeDef inputs, keep the original
    // behavior of clearing the edit-layer value opinion.
    const bool resetToDeclaredDefault = info.declared && info.hasDefaultValue;
    reset->setEnabled(resetToDeclaredDefault || info.hasAuthoredValue);
    reset->setToolTip(resetToDeclaredDefault ? QStringLiteral("Reset to the shader NodeDef default")
                                             : QStringLiteral("Clear the authored value and keep the input slot"));
    connect(reset, &QAction::triggered, d.tree.data(), [this, inputPath, info, resetToDeclaredDefault]() {
        if (resetToDeclaredDefault) {
            setInputValues({ inputPath }, info.defaultValue);
            return;
        }
        Q_EMIT d.tree->resetInputsRequested({ inputPath });
    });
    if (d.materials.size() == 1) {
        QMenu* add = menu.addMenu(QStringLiteral("Connect node"));

        MaterialMenu::Request request;
        request.targetType = info.typeName;

        // Keep this context menu in lockstep with MaterialGraph socket menus.
        // A MaterialX network should only offer MaterialX nodes; USD Preview
        // materials should only offer the curated USD Preview helper nodes.
        const bool materialX = d.materials.first().shaderId.startsWith(QStringLiteral("ND_"));
        request.materialXFirst = materialX;
        request.includeSearch = true;
        request.materialXOnly = materialX;
        request.usdPreviewOnly = !materialX;
        request.flattenFamily = true;

        MaterialMenu::populate(add, request, d.tree.data(), [this, inputPath](const MaterialMenu::Choice& choice) {
            if (choice.family == MaterialMenu::Family::MaterialX) {
                Q_EMIT d.tree->connectMaterialXNodeRequested(inputPath, choice.nodeDef, choice.nodeName);
            }
            else {
                Q_EMIT d.tree->connectShaderNodeRequested(inputPath, choice.shaderId, choice.nodeName,
                                                          choice.outputName);
            }
        });
    }
    menu.exec(d.tree->viewport()->mapToGlobal(position));
}

MaterialTree::MaterialTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new MaterialTreePrivate())
{
    p->d.tree = this;
}

MaterialTree::~MaterialTree() = default;

void
MaterialTree::setMaterials(const QList<MaterialEntry>& materials)
{
    p->ensureInitialized();
    const SdfPath previousMaterial = p->d.materials.size() == 1 ? p->d.materials.first().materialPath : SdfPath();
    const SdfPath newMaterial = materials.size() == 1 ? materials.first().materialPath : SdfPath();
    const bool sameSingleMaterial = !previousMaterial.IsEmpty() && previousMaterial == newMaterial
                                    && p->d.materials.size() == 1 && materials.size() == 1;


    p->d.materials = materials;

    if (previousMaterial != newMaterial) {
        p->d.history.clear();
        p->d.historyIndex = -1;
        p->d.currentNode = materials.size() == 1 ? materials.first().shaderPath : SdfPath();
        if (!p->d.currentNode.IsEmpty()) {
            p->d.history.append(p->d.currentNode);
            p->d.historyIndex = 0;
        }
    }

    // Value-only USD notices for the same material are by far the common case
    // while editing MaterialX. Keep the existing rows/widgets alive and update
    // their values in place. This preserves scroll position, focus, expanded
    // categories and the active editor without rebuilding the QTreeWidget.
    // If the node interface or connection topology changed, fall back to a full
    // rebuild so the tree remains authoritative.
    if (sameSingleMaterial && p->refreshSingleValues())
        return;

    p->rebuild(sameSingleMaterial);
}

void
MaterialTree::navigateToNode(const SdfPath& path)
{
    p->ensureInitialized();
    p->navigateTo(path);
}

MaterialNodeInfo
MaterialTree::currentNodeInfo() const
{
    return p->d.displayedNode;
}

void
MaterialTree::clearMaterials()
{
    p->ensureInitialized();
    p->d.materials.clear();
    p->d.currentNode = {};
    p->d.history.clear();
    p->d.historyIndex = -1;
    p->d.expandedGroupsByNode.clear();
    p->d.nodesWithExpansionState.clear();
    p->rebuild();
}

}  // namespace stageviz
