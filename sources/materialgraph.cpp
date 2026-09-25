// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialgraph.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "materialmenu.h"
#include "session.h"
#include "style.h"
#include "tracelocks.h"
#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QHash>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <vector>

// generated files
#include "ui_materialgraph.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialGraphPrivate : public QObject {
public:
    class GraphPortItem;
    class GraphNodeItem;
    class GraphScene : public QGraphicsScene {
    public:
        GraphScene() = default;
        using QGraphicsScene::QGraphicsScene;

    protected:
        void drawBackground(QPainter* painter, const QRectF& rect) override;
    };

    struct GraphEdge {
        GraphPortItem* output = nullptr;
        GraphPortItem* input = nullptr;
        QGraphicsPathItem* path = nullptr;
        QColor color;
    };

    explicit MaterialGraphPrivate(MaterialGraph* graph);
    void rebuild(bool preservePositions = true);
    void clear();
    void buildRecursive(const UsdStageRefPtr& stage, const SdfPath& nodePath, int depth, QSet<QString>& visited);
    void layoutNodes();
    void rebuildEdges();
    void updateEdges();
    void updateSceneRect();
    QRectF visibleItemsRect() const;
    QRectF selectedItemsRect() const;
    void zoom(qreal factor);
    void pan(const QPoint& delta);
    void resetView();
    void selectNode(const SdfPath& path);
    QList<SdfPath> selectedDeletableNodePaths() const;
    void deleteNodes(const QList<SdfPath>& paths);
    bool canDeleteNode(const SdfPath& path) const;
    void createMaterialXNode(const MaterialXNodeDefinition& definition, const QPointF& scenePos);
    void createFreeNode(const QString& shaderId, const QString& nodeName, const TfToken& outputName,
                        const SdfValueTypeName& outputType, const QPointF& scenePos);
    void showCreateNodeMenu(const QPoint& viewportPos);
    void beginConnection(GraphPortItem* output, const QPointF& scenePos);
    void updateConnection(const QPointF& scenePos);
    void finishConnection(GraphPortItem* output, const QPointF& scenePos);
    void cancelConnection();
    GraphPortItem* inputPortAt(const QPointF& scenePos) const;
    void disconnectInput(const SdfPath& path);
    void showInputNodeMenu(GraphPortItem* input, const QPoint& screenPos);
    bool compatibleConnection(GraphPortItem* output, GraphPortItem* input) const;
    void applyFilter(const QString& text);

public:
    struct Data {
        QPointer<MaterialGraph> graph;
        QScopedPointer<Ui_MaterialGraph> ui;
        QPointer<QGraphicsView> view;
        GraphScene scene;
        MaterialEntry material;
        SdfPath selectedNode;
        QHash<QString, GraphNodeItem*> nodes;
        QList<GraphEdge> edges;
        QGraphicsPathItem* dragPath = nullptr;
        GraphPortItem* dragOutput = nullptr;
        bool middlePan = false;
        QPoint lastPanPosition;
        bool viewInitialized = false;
        QRectF canvasRect { -50000.0, -50000.0, 100000.0, 100000.0 };
        bool pendingCreate = false;
        QPointF pendingCreatePosition;
        QSet<QString> pendingCreateExistingNodes;
        QSet<QString> connectedNodes;
        bool restoringSelection = false;
    };

    Data d;

    static QColor portColor(const SdfValueTypeName& type);
    static QString portTypeLabel(const SdfValueTypeName& type);
};

QColor
MaterialGraphPrivate::portColor(const SdfValueTypeName& type)
{
    QColor color;
    if (type == SdfValueTypeNames->Float || type == SdfValueTypeNames->Double || type == SdfValueTypeNames->Int)
        color = style()->color(Style::ColorRole::Warning);
    else if (type == SdfValueTypeNames->Color3f || type == SdfValueTypeNames->Color4f
             || type == SdfValueTypeNames->Float3 || type == SdfValueTypeNames->Float4)
        color = style()->color(Style::ColorRole::AxisY);
    else if (type == SdfValueTypeNames->Float2 || type == SdfValueTypeNames->TexCoord2f)
        color = style()->color(Style::ColorRole::AxisZ);
    else if (type == SdfValueTypeNames->Asset || type == SdfValueTypeNames->String || type == SdfValueTypeNames->Token)
        color = style()->color(Style::ColorRole::SelectionAlt);
    else
        color = style()->color(Style::ColorRole::ButtonAlt);

    return color.lighter(135);
}

QString
MaterialGraphPrivate::portTypeLabel(const SdfValueTypeName& type)
{
    if (type == SdfValueTypeNames->Float)
        return QStringLiteral("float");
    if (type == SdfValueTypeNames->Double)
        return QStringLiteral("double");
    if (type == SdfValueTypeNames->Int)
        return QStringLiteral("int");
    if (type == SdfValueTypeNames->Bool)
        return QStringLiteral("bool");
    if (type == SdfValueTypeNames->Color3f)
        return QStringLiteral("color3");
    if (type == SdfValueTypeNames->Color4f)
        return QStringLiteral("color4");
    if (type == SdfValueTypeNames->Float2 || type == SdfValueTypeNames->TexCoord2f)
        return QStringLiteral("vector2");
    if (type == SdfValueTypeNames->Float3)
        return QStringLiteral("vector3");
    if (type == SdfValueTypeNames->Float4)
        return QStringLiteral("vector4");
    if (type == SdfValueTypeNames->Asset)
        return QStringLiteral("filename");
    if (type == SdfValueTypeNames->String)
        return QStringLiteral("string");
    if (type == SdfValueTypeNames->Token)
        return QStringLiteral("token");

    const std::string token = type.GetAsToken().GetString();
    return token.empty() ? QString() : QString::fromStdString(token);
}

void
MaterialGraphPrivate::GraphScene::drawBackground(QPainter* painter, const QRectF& rect)
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

class MaterialGraphPrivate::GraphPortItem : public QGraphicsEllipseItem {
public:
    enum Kind { Input, Output };

    GraphPortItem(MaterialGraphPrivate* owner, Kind kind, const SdfPath& path, const SdfValueTypeName& type,
                  QGraphicsItem* parent = nullptr)
        : QGraphicsEllipseItem(parent)
        , m_owner(owner)
        , m_kind(kind)
        , m_path(path)
        , m_type(type)
    {
        setRect(-5, -5, 10, 10);
        const QColor color = MaterialGraphPrivate::portColor(type);
        setBrush(color);
        setPen(QPen(style()->color(Style::ColorRole::BorderAlt), 1.0));
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        setToolTip(QString::fromStdString(path.GetString()));
        setZValue(20.0);
    }

    Kind kind() const { return m_kind; }
    SdfPath path() const { return m_path; }
    SdfValueTypeName typeName() const { return m_type; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (m_kind == Output && event->button() == Qt::LeftButton && m_owner) {
            m_owner->beginConnection(this, event->scenePos());
            event->accept();
            return;
        }
        QGraphicsEllipseItem::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (m_kind == Output && m_owner) {
            m_owner->updateConnection(event->scenePos());
            event->accept();
            return;
        }
        QGraphicsEllipseItem::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (m_kind == Output && m_owner && event->button() == Qt::LeftButton) {
            m_owner->finishConnection(this, event->scenePos());
            event->accept();
            return;
        }
        QGraphicsEllipseItem::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override
    {
        if (m_kind != Input || !m_owner) {
            QGraphicsEllipseItem::contextMenuEvent(event);
            return;
        }

        m_owner->showInputNodeMenu(this, event->screenPos());
        event->accept();
    }

private:
    MaterialGraphPrivate* m_owner = nullptr;
    Kind m_kind = Input;
    SdfPath m_path;
    SdfValueTypeName m_type;
};

class MaterialGraphPrivate::GraphNodeItem : public QGraphicsRectItem {
public:
    GraphNodeItem(MaterialGraphPrivate* owner, const SdfPath& path, const QString& name, const QString& type,
                  QGraphicsItem* parent = nullptr)
        : QGraphicsRectItem(parent)
        , m_owner(owner)
        , m_path(path)
        , m_name(name)
        , m_typeName(type)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemIsMovable, true);
        setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        setPen(Qt::NoPen);
        setBrush(Qt::NoBrush);

        QFont titleFont = app()->font();
        titleFont.setPixelSize(style()->fontSize(Style::UIScale::Medium));
        titleFont.setBold(true);
        m_title = new QGraphicsSimpleTextItem(name, this);
        m_title->setFont(titleFont);
        m_title->setBrush(style()->color(Style::ColorRole::Text));
        m_title->setAcceptedMouseButtons(Qt::NoButton);

        QFont typeFont = app()->font();
        typeFont.setPixelSize(style()->fontSize(Style::UIScale::Small));
        m_type = new QGraphicsSimpleTextItem(type, this);
        m_type->setFont(typeFont);
        m_type->setBrush(style()->color(Style::ColorRole::Text, Style::UIState::Disabled));
        m_type->setAcceptedMouseButtons(Qt::NoButton);

        relayout();
    }

    SdfPath path() const { return m_path; }
    QString name() const { return m_name; }
    QString typeName() const { return m_typeName; }
    QList<GraphPortItem*> inputs() const { return m_inputs; }
    QList<GraphPortItem*> outputs() const { return m_outputs; }

    GraphPortItem* addInput(const QString& label, const SdfPath& path, const SdfValueTypeName& type)
    {
        auto* port = new GraphPortItem(m_owner, GraphPortItem::Input, path, type, this);
        auto* text = new QGraphicsSimpleTextItem(label, this);
        auto* typeText = new QGraphicsSimpleTextItem(MaterialGraphPrivate::portTypeLabel(type), this);

        QFont font = app()->font();
        font.setPixelSize(style()->fontSize(Style::UIScale::Small));
        text->setFont(font);
        text->setBrush(style()->color(Style::ColorRole::Text));
        text->setAcceptedMouseButtons(Qt::NoButton);

        QFont typeFont = font;
        typeFont.setPixelSize(std::max(8, style()->fontSize(Style::UIScale::Small) - 1));
        typeText->setFont(typeFont);
        typeText->setBrush(style()->color(Style::ColorRole::Text, Style::UIState::Disabled));
        typeText->setAcceptedMouseButtons(Qt::NoButton);

        m_inputs.append(port);
        m_inputTexts.append(text);
        m_inputTypeTexts.append(typeText);
        relayout();
        return port;
    }

    GraphPortItem* addOutput(const QString& label, const SdfPath& path, const SdfValueTypeName& type)
    {
        auto* port = new GraphPortItem(m_owner, GraphPortItem::Output, path, type, this);
        auto* text = new QGraphicsSimpleTextItem(label, this);
        auto* typeText = new QGraphicsSimpleTextItem(MaterialGraphPrivate::portTypeLabel(type), this);

        QFont font = app()->font();
        font.setPixelSize(style()->fontSize(Style::UIScale::Small));
        text->setFont(font);
        text->setBrush(style()->color(Style::ColorRole::Text));
        text->setAcceptedMouseButtons(Qt::NoButton);

        QFont typeFont = font;
        typeFont.setPixelSize(std::max(8, style()->fontSize(Style::UIScale::Small) - 1));
        typeText->setFont(typeFont);
        typeText->setBrush(style()->color(Style::ColorRole::Text, Style::UIState::Disabled));
        typeText->setAcceptedMouseButtons(Qt::NoButton);

        m_outputs.append(port);
        m_outputTexts.append(text);
        m_outputTypeTexts.append(typeText);
        relayout();
        return port;
    }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        constexpr qreal radius = 6.0;
        constexpr qreal headerHeight = 48.0;
        const QRectF r = rect();

        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(style()->color(Style::ColorRole::Item));
        painter->drawRoundedRect(r, radius, radius);

        QPainterPath headerPath;
        headerPath.addRoundedRect(QRectF(r.left(), r.top(), r.width(), headerHeight + radius), radius, radius);
        headerPath.addRect(QRectF(r.left(), r.top() + headerHeight - radius, r.width(), radius * 2.0));
        painter->setBrush(style()->color(Style::ColorRole::ItemAlt));
        painter->drawPath(headerPath);

        QColor border = isSelected() ? style()->color(Style::ColorRole::Highlight)
                                     : style()->color(Style::ColorRole::BorderAlt);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(border, isSelected() ? 2.0 : 1.0));
        painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override
    {
        if (!m_owner || !event) {
            QGraphicsRectItem::contextMenuEvent(event);
            return;
        }

        QList<SdfPath> deletePaths;
        if (isSelected())
            deletePaths = m_owner->selectedDeletableNodePaths();
        if (deletePaths.isEmpty() && m_owner->canDeleteNode(m_path))
            deletePaths.append(m_path);

        QMenu menu;
        QAction* remove = menu.addAction(deletePaths.size() > 1 ? QObject::tr("Delete Nodes")
                                                                : QObject::tr("Delete Node"));
        remove->setEnabled(!deletePaths.isEmpty());
        if (menu.exec(event->screenPos()) == remove)
            m_owner->deleteNodes(deletePaths);
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        QGraphicsRectItem::mouseReleaseEvent(event);

        // Do not resize the scene while the node is being dragged.
        // Changing sceneRect() during a move forces QGraphicsView to
        // recalculate its scroll bars and can shift the scene-to-mouse
        // mapping, which makes the node visibly jump or flicker. Update
        // the scrollable canvas once the move has finished instead.
        if (m_owner)
            m_owner->updateSceneRect();
    }

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if (change == QGraphicsItem::ItemSelectedHasChanged) {
            update();
            if (value.toBool() && m_owner)
                m_owner->selectNode(m_path);
        }
        if (change == QGraphicsItem::ItemPositionHasChanged && m_owner)
            m_owner->updateEdges();
        return QGraphicsRectItem::itemChange(change, value);
    }

private:
    void relayout()
    {
        constexpr qreal width = 280.0;
        constexpr qreal header = 48.0;
        constexpr qreal rowHeight = 24.0;
        const int rows = std::max(static_cast<int>(m_inputs.size()), static_cast<int>(m_outputs.size()));
        const qreal height = header + std::max(1, rows) * rowHeight + 10.0;
        setRect(0, 0, width, height);

        m_title->setPos(12.0, 6.0);
        m_type->setPos(12.0, 27.0);

        constexpr qreal portInset = 13.0;
        constexpr qreal typeGap = 7.0;

        for (int i = 0; i < m_inputs.size(); ++i) {
            const qreal y = header + rowHeight * i + rowHeight * 0.5;
            m_inputs[i]->setPos(0.0, y);
            m_inputTexts[i]->setPos(portInset, y - 9.0);

            if (i < m_inputTypeTexts.size()) {
                const QRectF labelBounds = m_inputTexts[i]->boundingRect();
                m_inputTypeTexts[i]->setPos(portInset + labelBounds.width() + typeGap, y - 8.0);
            }
        }

        for (int i = 0; i < m_outputs.size(); ++i) {
            const qreal y = header + rowHeight * i + rowHeight * 0.5;
            m_outputs[i]->setPos(width, y);

            const QRectF labelBounds = m_outputTexts[i]->boundingRect();
            const QRectF typeBounds = i < m_outputTypeTexts.size() ? m_outputTypeTexts[i]->boundingRect() : QRectF();
            const qreal totalWidth = labelBounds.width() + (typeBounds.isEmpty() ? 0.0 : typeGap + typeBounds.width());
            const qreal labelX = width - portInset - totalWidth;
            m_outputTexts[i]->setPos(labelX, y - 9.0);

            if (i < m_outputTypeTexts.size())
                m_outputTypeTexts[i]->setPos(labelX + labelBounds.width() + typeGap, y - 8.0);
        }
    }
    MaterialGraphPrivate* m_owner = nullptr;
    SdfPath m_path;
    QString m_name;
    QString m_typeName;
    QGraphicsSimpleTextItem* m_title = nullptr;
    QGraphicsSimpleTextItem* m_type = nullptr;
    QList<GraphPortItem*> m_inputs;
    QList<GraphPortItem*> m_outputs;
    QList<QGraphicsSimpleTextItem*> m_inputTexts;
    QList<QGraphicsSimpleTextItem*> m_inputTypeTexts;
    QList<QGraphicsSimpleTextItem*> m_outputTexts;
    QList<QGraphicsSimpleTextItem*> m_outputTypeTexts;
};

MaterialGraphPrivate::MaterialGraphPrivate(MaterialGraph* graph)
    : QObject(graph)
{
    d.graph = graph;
}


void
MaterialGraphPrivate::clear()
{
    d.scene.clear();
    d.nodes.clear();
    d.edges.clear();
    d.dragPath = nullptr;
    d.dragOutput = nullptr;
    d.selectedNode = SdfPath();
    d.connectedNodes.clear();
}

void
MaterialGraphPrivate::buildRecursive(const UsdStageRefPtr& stage, const SdfPath& nodePath, int depth,
                                     QSet<QString>& visited)
{
    if (!stage || nodePath.IsEmpty() || depth > 32)
        return;

    const QString key = QString::fromStdString(nodePath.GetString());
    if (visited.contains(key))
        return;
    visited.insert(key);

    const UsdPrim prim = stage->GetPrimAtPath(nodePath);
    const UsdShadeConnectableAPI connectable(prim);
    if (!prim || !connectable)
        return;

    QString type = MaterialUtils::shaderId(prim);
    if (type.isEmpty())
        type = QString::fromStdString(prim.GetTypeName().GetString());

    auto* node = new GraphNodeItem(this, nodePath, QString::fromStdString(prim.GetName().GetString()), type);
    d.scene.addItem(node);
    d.nodes.insert(key, node);

    QSet<QString> declaredInputs;
    QSet<QString> declaredOutputs;
    if (type.startsWith(QStringLiteral("ND_"))) {
        static const QHash<QString, MaterialXNodeDefinition> definitionsById = []() {
            QHash<QString, MaterialXNodeDefinition> definitions;
            const QList<MaterialXNodeDefinition> defs = MaterialUtils::materialXNodeDefinitions();
            definitions.reserve(defs.size());
            for (const MaterialXNodeDefinition& def : defs)
                definitions.insert(def.nodeDef, def);
            return definitions;
        }();

        const auto definitionIt = definitionsById.constFind(type);
        if (definitionIt != definitionsById.cend()) {
            const MaterialXNodeDefinition& definition = definitionIt.value();
            for (const MaterialXPortDefinition& port : definition.inputs) {
                if (port.name.isEmpty() || declaredInputs.contains(port.name))
                    continue;
                declaredInputs.insert(port.name);
                const SdfPath path = nodePath.AppendProperty(
                    TfToken((std::string("inputs:") + port.name.toStdString())));
                node->addInput(port.name, path, MaterialUtils::sdfTypeForMaterialX(port.type));
            }

            for (const MaterialXPortDefinition& port : definition.outputs) {
                if (port.name.isEmpty() || declaredOutputs.contains(port.name))
                    continue;
                declaredOutputs.insert(port.name);
                const SdfPath path = nodePath.AppendProperty(
                    TfToken((std::string("outputs:") + port.name.toStdString())));
                node->addOutput(port.name, path, MaterialUtils::sdfTypeForMaterialX(port.type));
            }
        }
    }
    for (const UsdShadeInput& input : connectable.GetInputs()) {
        const QString name = QString::fromStdString(input.GetBaseName().GetString());
        if (!declaredInputs.contains(name)) {
            declaredInputs.insert(name);
            node->addInput(name, input.GetAttr().GetPath(), input.GetTypeName());
        }

        UsdShadeConnectableAPI source;
        TfToken sourceName;
        UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
        if (MaterialUtils::resolveSource(input, &source, &sourceName, &sourceType))
            buildRecursive(stage, source.GetPrim().GetPath(), depth + 1, visited);
    }

    for (const UsdShadeOutput& output : connectable.GetOutputs()) {
        const QString name = QString::fromStdString(output.GetBaseName().GetString());
        if (declaredOutputs.contains(name))
            continue;
        declaredOutputs.insert(name);
        node->addOutput(name, output.GetAttr().GetPath(), output.GetTypeName());
    }
}

void
MaterialGraphPrivate::layoutNodes()
{
    if (d.material.shaderPath.IsEmpty() || d.nodes.isEmpty())
        return;

    QHash<QString, QList<QString>> upstream;
    QHash<QString, int> depths;
    QSet<QString> connected;
    const QString rootKey = QString::fromStdString(d.material.shaderPath.GetString());
    depths.insert(rootKey, 0);

    UsdStageRefPtr stage;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        stage = session()->stageUnsafe();
        if (!stage)
            return;

        QList<QString> queue { rootKey };
        for (int index = 0; index < queue.size(); ++index) {
            const QString key = queue[index];
            if (connected.contains(key))
                continue;
            connected.insert(key);

            GraphNodeItem* node = d.nodes.value(key);
            if (!node)
                continue;

            const UsdPrim prim = stage->GetPrimAtPath(node->path());
            const UsdShadeConnectableAPI connectable(prim);
            if (!connectable)
                continue;

            QList<QString> children;
            for (const UsdShadeInput& input : connectable.GetInputs()) {
                UsdShadeConnectableAPI source;
                TfToken sourceName;
                UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
                if (!MaterialUtils::resolveSource(input, &source, &sourceName, &sourceType))
                    continue;

                const QString sourceKey = QString::fromStdString(source.GetPrim().GetPath().GetString());
                if (!d.nodes.contains(sourceKey))
                    continue;

                if (!children.contains(sourceKey))
                    children.append(sourceKey);

                const int sourceDepth = depths.value(key, 0) + 1;
                if (!depths.contains(sourceKey) || sourceDepth > depths.value(sourceKey))
                    depths[sourceKey] = sourceDepth;

                if (!queue.contains(sourceKey))
                    queue.append(sourceKey);
            }
            upstream.insert(key, children);
        }
    }

    int maxDepth = 0;
    for (auto it = depths.cbegin(); it != depths.cend(); ++it)
        maxDepth = std::max(maxDepth, it.value());

    constexpr qreal columnSpacing = 330.0;
    constexpr qreal rowSpacing = 42.0;
    constexpr qreal freeGroupGap = 120.0;

    QHash<QString, qreal> desiredCenter;
    QSet<QString> placing;
    QSet<QString> placed;
    qreal cursorY = 0.0;

    std::function<qreal(const QString&)> placeBranch = [&](const QString& key) -> qreal {
        GraphNodeItem* node = d.nodes.value(key);
        if (!node)
            return cursorY;

        if (desiredCenter.contains(key))
            return desiredCenter.value(key);

        if (placing.contains(key)) {
            const qreal center = cursorY + node->rect().height() * 0.5;
            cursorY += node->rect().height() + rowSpacing;
            desiredCenter.insert(key, center);
            return center;
        }

        placing.insert(key);

        QList<qreal> childCenters;
        const QList<QString> children = upstream.value(key);
        for (const QString& childKey : children)
            childCenters.append(placeBranch(childKey));

        qreal center = 0.0;
        if (childCenters.isEmpty()) {
            center = cursorY + node->rect().height() * 0.5;
            cursorY += node->rect().height() + rowSpacing;
        }
        else {
            center = (childCenters.first() + childCenters.last()) * 0.5;
        }

        placing.remove(key);
        placed.insert(key);
        desiredCenter.insert(key, center);
        return center;
    };

    if (d.nodes.contains(rootKey))
        placeBranch(rootKey);

    const qreal connectedBottom = cursorY;
    qreal freeCursorY = connectedBottom + freeGroupGap;
    QList<QString> freeKeys;
    for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it) {
        if (!connected.contains(it.key()))
            freeKeys.append(it.key());
    }
    std::sort(freeKeys.begin(), freeKeys.end());
    for (const QString& key : freeKeys) {
        GraphNodeItem* node = d.nodes.value(key);
        if (!node)
            continue;
        desiredCenter.insert(key, freeCursorY + node->rect().height() * 0.5);
        depths.insert(key, maxDepth + 1);
        freeCursorY += node->rect().height() + rowSpacing;
    }

    QHash<int, QList<QString>> columns;
    for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it)
        columns[depths.value(it.key(), maxDepth + 1)].append(it.key());

    for (auto columnIt = columns.begin(); columnIt != columns.end(); ++columnIt) {
        QList<QString>& keys = columnIt.value();
        std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
            const qreal ay = desiredCenter.value(a);
            const qreal by = desiredCenter.value(b);
            if (!qFuzzyCompare(ay + 1.0, by + 1.0))
                return ay < by;
            return a < b;
        });

        qreal previousBottom = -1.0e12;
        for (const QString& key : keys) {
            GraphNodeItem* node = d.nodes.value(key);
            if (!node)
                continue;

            const qreal height = node->rect().height();
            qreal top = desiredCenter.value(key) - height * 0.5;
            top = std::max(top, previousBottom + rowSpacing);
            desiredCenter[key] = top + height * 0.5;
            previousBottom = top + height;
        }
    }

    d.connectedNodes = connected;
    for (auto it = d.nodes.begin(); it != d.nodes.end(); ++it) {
        const QString key = it.key();
        GraphNodeItem* node = it.value();
        if (!node)
            continue;

        const int depth = depths.value(key, maxDepth + 1);
        const qreal x = (maxDepth - depth) * columnSpacing;
        const qreal y = desiredCenter.value(key) - node->rect().height() * 0.5;
        node->setPos(x, y);
    }
}

void
MaterialGraphPrivate::rebuildEdges()
{
    d.edges.clear();

    UsdStageRefPtr stage;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        stage = session()->stageUnsafe();
        if (!stage)
            return;

        for (auto it = d.nodes.begin(); it != d.nodes.end(); ++it) {
            GraphNodeItem* destinationNode = it.value();
            const UsdPrim prim = stage->GetPrimAtPath(destinationNode->path());
            const UsdShadeConnectableAPI connectable(prim);
            if (!connectable)
                continue;

            const QList<GraphPortItem*> inputPorts = destinationNode->inputs();
            const auto inputs = connectable.GetInputs();
            for (const UsdShadeInput& input : inputs) {
                GraphPortItem* inputPort = nullptr;
                for (GraphPortItem* port : inputPorts) {
                    if (port && port->path() == input.GetAttr().GetPath()) {
                        inputPort = port;
                        break;
                    }
                }
                if (!inputPort)
                    continue;

                UsdShadeConnectableAPI source;
                TfToken sourceName;
                UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
                if (!MaterialUtils::resolveSource(input, &source, &sourceName, &sourceType))
                    continue;

                GraphNodeItem* sourceNode = d.nodes.value(
                    QString::fromStdString(source.GetPrim().GetPath().GetString()));
                if (!sourceNode)
                    continue;

                GraphPortItem* sourcePort = nullptr;
                for (GraphPortItem* port : sourceNode->outputs()) {
                    if (port->path().GetNameToken() == TfToken("outputs:" + sourceName.GetString())) {
                        sourcePort = port;
                        break;
                    }
                }
                if (!sourcePort) {
                    const UsdShadeOutput output = source.GetOutput(sourceName);
                    if (output) {
                        for (GraphPortItem* port : sourceNode->outputs()) {
                            if (port->path() == output.GetAttr().GetPath()) {
                                sourcePort = port;
                                break;
                            }
                        }
                    }
                }
                if (!sourcePort)
                    continue;

                GraphEdge edge;
                edge.output = sourcePort;
                edge.input = inputPort;
                edge.color = portColor(sourcePort->typeName());
                edge.path = d.scene.addPath(QPainterPath(), QPen(edge.color, 2.0));
                edge.path->setZValue(-10.0);
                d.edges.append(edge);
            }
        }
    }
    updateEdges();
    updateSceneRect();
}

void
MaterialGraphPrivate::updateEdges()
{
    for (GraphEdge& edge : d.edges) {
        if (!edge.output || !edge.input || !edge.path)
            continue;
        const QPointF start = edge.output->scenePos();
        const QPointF end = edge.input->scenePos();
        QPainterPath path(start);
        const qreal dx = std::max<qreal>(50.0, std::abs(end.x() - start.x()) * 0.5);
        path.cubicTo(start + QPointF(dx, 0.0), end - QPointF(dx, 0.0), end);
        edge.path->setPath(path);
    }
}

QRectF
MaterialGraphPrivate::visibleItemsRect() const
{
    QRectF bounds;
    bool haveBounds = false;

    for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it) {
        GraphNodeItem* node = it.value();
        if (!node || !node->isVisible())
            continue;
        bounds = haveBounds ? bounds.united(node->sceneBoundingRect()) : node->sceneBoundingRect();
        haveBounds = true;
    }

    for (const GraphEdge& edge : d.edges) {
        if (!edge.path || !edge.path->isVisible())
            continue;
        bounds = haveBounds ? bounds.united(edge.path->sceneBoundingRect()) : edge.path->sceneBoundingRect();
        haveBounds = true;
    }

    return haveBounds ? bounds : QRectF();
}

QRectF
MaterialGraphPrivate::selectedItemsRect() const
{
    QRectF bounds;
    bool haveBounds = false;

    for (QGraphicsItem* item : d.scene.selectedItems()) {
        auto* node = dynamic_cast<GraphNodeItem*>(item);
        if (!node || !node->isVisible())
            continue;
        bounds = haveBounds ? bounds.united(node->sceneBoundingRect()) : node->sceneBoundingRect();
        haveBounds = true;
    }

    return haveBounds ? bounds : QRectF();
}

void
MaterialGraphPrivate::applyFilter(const QString& text)
{
    const QString filter = text.trimmed();

    for (auto it = d.nodes.begin(); it != d.nodes.end(); ++it) {
        GraphNodeItem* node = it.value();
        if (!node)
            continue;

        const QString path = QString::fromStdString(node->path().GetString());
        const bool visible = filter.isEmpty() || node->name().contains(filter, Qt::CaseInsensitive)
                             || node->typeName().contains(filter, Qt::CaseInsensitive)
                             || path.contains(filter, Qt::CaseInsensitive);
        node->setVisible(visible);
    }

    for (GraphEdge& edge : d.edges) {
        if (!edge.path)
            continue;

        const QGraphicsItem* outputNode = edge.output ? edge.output->parentItem() : nullptr;
        const QGraphicsItem* inputNode = edge.input ? edge.input->parentItem() : nullptr;
        edge.path->setVisible(outputNode && inputNode && outputNode->isVisible() && inputNode->isVisible());
    }

    updateSceneRect();
    if (d.view)
        d.view->viewport()->update();
}

void
MaterialGraphPrivate::updateSceneRect()
{
    QRectF required = visibleItemsRect();
    if (!required.isNull() && !required.isEmpty()) {
        constexpr qreal growMargin = 4000.0;
        required.adjust(-growMargin, -growMargin, growMargin, growMargin);
        if (!d.canvasRect.contains(required))
            d.canvasRect = d.canvasRect.united(required);
    }
    d.scene.setSceneRect(d.canvasRect);
}

void
MaterialGraphPrivate::rebuild(bool preservePositions)
{
    QHash<QString, QPointF> previousPositions;
    QSet<QString> previousSelection;
    const SdfPath previousPrimarySelection = d.selectedNode;
    for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it) {
        if (!it.value())
            continue;
        if (preservePositions)
            previousPositions.insert(it.key(), it.value()->pos());
        if (it.value()->isSelected())
            previousSelection.insert(it.key());
    }

    // USD notice rebuilds preserve layout; explicit rebuilds regenerate it.
    const bool restoreViewport = preservePositions && d.viewInitialized && d.view;
    const int previousHorizontal = restoreViewport ? d.view->horizontalScrollBar()->value() : 0;
    const int previousVertical = restoreViewport ? d.view->verticalScrollBar()->value() : 0;

    clear();

    if (d.material.shaderPath.IsEmpty())
        return;

    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;
        QSet<QString> visited;
        buildRecursive(stage, d.material.shaderPath, 0, visited);

        const UsdPrim materialPrim = stage->GetPrimAtPath(d.material.materialPath);
        if (materialPrim) {
            for (const UsdPrim& prim : UsdPrimRange(materialPrim)) {
                if (prim == materialPrim || !prim.IsA<UsdShadeShader>())
                    continue;
                const QString key = QString::fromStdString(prim.GetPath().GetString());
                if (!visited.contains(key))
                    buildRecursive(stage, prim.GetPath(), 0, visited);
            }
        }
    }
    layoutNodes();

    if (!previousPositions.isEmpty()) {
        const QString rootKey = QString::fromStdString(d.material.shaderPath.GetString());
        QPointF layoutOffset;
        bool haveLayoutOffset = false;
        if (previousPositions.contains(rootKey)) {
            if (GraphNodeItem* root = d.nodes.value(rootKey)) {
                layoutOffset = previousPositions.value(rootKey) - root->pos();
                haveLayoutOffset = true;
            }
        }

        if (haveLayoutOffset && !d.pendingCreate) {
            for (auto it = d.nodes.begin(); it != d.nodes.end(); ++it) {
                if (!it.value() || previousPositions.contains(it.key()))
                    continue;
                it.value()->setPos(it.value()->pos() + layoutOffset);
            }
        }

        // Existing nodes always keep the exact user-authored position across
        // topology changes. Connecting or disconnecting a node must never
        // silently re-run layout for that node. Only genuinely new nodes use
        // the automatic/pending-create placement below.
        for (auto it = previousPositions.cbegin(); it != previousPositions.cend(); ++it) {
            if (GraphNodeItem* node = d.nodes.value(it.key()))
                node->setPos(it.value());
        }
    }

    // New nodes are positioned after the synchronous USD notice rebuild.
    if (d.pendingCreate) {
        int createdIndex = 0;
        for (auto it = d.nodes.begin(); it != d.nodes.end(); ++it) {
            if (d.pendingCreateExistingNodes.contains(it.key()) || !it.value())
                continue;

            it.value()->setPos(d.pendingCreatePosition + QPointF(createdIndex * 28.0, createdIndex * 28.0));
            ++createdIndex;
        }
        d.pendingCreate = false;
        d.pendingCreateExistingNodes.clear();
    }

    d.restoringSelection = true;
    for (const QString& key : previousSelection) {
        if (GraphNodeItem* node = d.nodes.value(key))
            node->setSelected(true);
    }
    if (!previousPrimarySelection.IsEmpty()) {
        const QString primaryKey = QString::fromStdString(previousPrimarySelection.GetString());
        if (GraphNodeItem* primary = d.nodes.value(primaryKey)) {
            primary->setSelected(true);
            d.selectedNode = previousPrimarySelection;
        }
    }
    d.restoringSelection = false;

    rebuildEdges();
    applyFilter(d.ui ? d.ui->filter->text() : QString());
    updateSceneRect();

    if (d.graph && !d.viewInitialized) {
        d.viewInitialized = true;
        QPointer<MaterialGraph> graph = d.graph;
        QTimer::singleShot(0, d.graph.data(), [graph]() {
            if (graph)
                graph->frameAll();
        });
    }
    else if (restoreViewport && d.view) {
        // sceneRect is intentionally stable, so restoring the exact scrollbar
        // values keeps the graph pixel-stationary across setAttribute notices.
        d.view->horizontalScrollBar()->setValue(previousHorizontal);
        d.view->verticalScrollBar()->setValue(previousVertical);
    }
}

void
MaterialGraphPrivate::beginConnection(GraphPortItem* output, const QPointF& scenePos)
{
    cancelConnection();
    d.dragOutput = output;
    const QColor dragColor = output ? portColor(output->typeName()) : style()->color(Style::ColorRole::Highlight);
    d.dragPath = d.scene.addPath(QPainterPath(), QPen(dragColor, 2.4));
    d.dragPath->setZValue(100.0);
    updateConnection(scenePos);
}

void
MaterialGraphPrivate::updateConnection(const QPointF& scenePos)
{
    if (!d.dragOutput || !d.dragPath)
        return;

    const QPointF start = d.dragOutput->scenePos();
    QPainterPath path(start);
    const qreal dx = std::max<qreal>(50.0, std::abs(scenePos.x() - start.x()) * 0.5);
    path.cubicTo(start + QPointF(dx, 0.0), scenePos - QPointF(dx, 0.0), scenePos);
    d.dragPath->setPath(path);

    QColor color = portColor(d.dragOutput->typeName());
    Qt::PenStyle penStyle = Qt::SolidLine;
    qreal width = 2.4;

    if (GraphPortItem* input = inputPortAt(scenePos)) {
        if (!compatibleConnection(d.dragOutput, input)) {
            color = style()->color(Style::ColorRole::Error);
            penStyle = Qt::DashLine;
            width = 2.8;
        }
    }

    QPen pen(color, width, penStyle, Qt::RoundCap, Qt::RoundJoin);
    d.dragPath->setPen(pen);
}

MaterialGraphPrivate::GraphPortItem*
MaterialGraphPrivate::inputPortAt(const QPointF& scenePos) const
{
    const QList<QGraphicsItem*> items = d.scene.items(scenePos);
    for (QGraphicsItem* item : items) {
        auto* port = dynamic_cast<GraphPortItem*>(item);
        if (port && port->kind() == GraphPortItem::Input)
            return port;
    }
    return nullptr;
}

void
MaterialGraphPrivate::finishConnection(GraphPortItem* output, const QPointF& scenePos)
{
    GraphPortItem* input = inputPortAt(scenePos);
    if (output && input && compatibleConnection(output, input) && d.graph)
        Q_EMIT d.graph->connectionRequested(input->path(), output->path());
    cancelConnection();
}

void
MaterialGraphPrivate::cancelConnection()
{
    if (d.dragPath) {
        d.scene.removeItem(d.dragPath);
        delete d.dragPath;
    }
    d.dragPath = nullptr;
    d.dragOutput = nullptr;
}

void
MaterialGraphPrivate::selectNode(const SdfPath& path)
{
    d.selectedNode = path;
    if (!d.restoringSelection && d.graph)
        Q_EMIT d.graph->nodeSelected(path);
}

void
MaterialGraphPrivate::disconnectInput(const SdfPath& path)
{
    if (d.graph)
        Q_EMIT d.graph->disconnectRequested(path);
}

void
MaterialGraphPrivate::showInputNodeMenu(GraphPortItem* input, const QPoint& screenPos)
{
    if (!input || !d.graph || d.material.materialPath.IsEmpty())
        return;

    QMenu menu(d.graph.data());
    QMenu* connectMenu = menu.addMenu(QObject::tr("Connect Node"));

    MaterialMenu::Request request;
    request.targetType = input->typeName();

    const bool materialX = d.material.shaderId.startsWith(QStringLiteral("ND_"));
    request.materialXFirst = materialX;
    request.includeSearch = true;
    request.materialXOnly = materialX;
    request.usdPreviewOnly = !materialX;
    request.flattenFamily = true;

    MaterialMenu::populate(connectMenu, request, d.graph.data(), [this, input](const MaterialMenu::Choice& choice) {
        d.pendingCreate = true;
        d.pendingCreatePosition = input->scenePos() + QPointF(-320.0, -40.0);
        d.pendingCreateExistingNodes.clear();
        for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it)
            d.pendingCreateExistingNodes.insert(it.key());

        if (choice.family == MaterialMenu::Family::MaterialX) {
            Q_EMIT d.graph->connectMaterialXNodeRequested(input->path(), choice.nodeDef, choice.nodeName);
        }
        else {
            Q_EMIT d.graph->connectShaderNodeRequested(input->path(), choice.shaderId, choice.nodeName,
                                                       choice.outputName);
        }
    });

    bool isConnected = false;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (stage) {
            const MaterialNodeInfo node = MaterialUtils::nodeInfo(stage, input->path().GetPrimPath());
            for (const MaterialInputInfo& candidate : node.inputs) {
                if (candidate.inputPath == input->path()) {
                    isConnected = candidate.connected;
                    break;
                }
            }
        }
    }

    menu.addSeparator();
    QAction* disconnect = menu.addAction(QObject::tr("Disconnect"));
    disconnect->setEnabled(isConnected);
    QObject::connect(disconnect, &QAction::triggered, d.graph.data(),
                     [this, path = input->path()]() { disconnectInput(path); });

    menu.exec(screenPos);
}

bool
MaterialGraphPrivate::compatibleConnection(GraphPortItem* output, GraphPortItem* input) const
{
    if (!output || !input)
        return false;

    READ_LOCKER(locker, session()->stageLock(), "stageLock");
    const UsdStageRefPtr stage = session()->stageUnsafe();
    if (!stage)
        return false;

    const MaterialNodeInfo sourceNode = MaterialUtils::nodeInfo(stage, output->path().GetPrimPath());
    const MaterialNodeInfo targetNode = MaterialUtils::nodeInfo(stage, input->path().GetPrimPath());

    auto isUsdPreviewShader = [](const QString& shaderId) {
        return shaderId.startsWith(QStringLiteral("Usd"), Qt::CaseInsensitive)
               || shaderId.startsWith(QStringLiteral("ND_Usd"), Qt::CaseInsensitive);
    };
    auto isMaterialXShader = [&](const QString& shaderId) {
        return shaderId.startsWith(QStringLiteral("ND_"), Qt::CaseInsensitive) && !isUsdPreviewShader(shaderId);
    };

    const bool sourceUsd = isUsdPreviewShader(sourceNode.shaderId);
    const bool targetUsd = isUsdPreviewShader(targetNode.shaderId);
    const bool sourceMaterialX = isMaterialXShader(sourceNode.shaderId);
    const bool targetMaterialX = isMaterialXShader(targetNode.shaderId);

    if ((sourceUsd && targetMaterialX) || (sourceMaterialX && targetUsd)) {
        return false;
    }

    const bool materialXToMaterialX = sourceMaterialX && targetMaterialX;

    if (sourceUsd && targetUsd && output->typeName() == SdfValueTypeNames->Float3
        && input->typeName() == SdfValueTypeNames->Color3f) {
        const bool uvTextureRgb = sourceNode.shaderId == QStringLiteral("UsdUVTexture")
                                  && output->path().GetNameToken() == TfToken("outputs:rgb");
        if (uvTextureRgb)
            return true;

        return false;
    }

    return MaterialMenu::compatibility(output->typeName(), input->typeName(), materialXToMaterialX)
           == MaterialMenu::Compatibility::Compatible;
}

QList<SdfPath>
MaterialGraphPrivate::selectedDeletableNodePaths() const
{
    QList<SdfPath> paths;
    QSet<QString> seen;

    for (QGraphicsItem* item : d.scene.selectedItems()) {
        auto* node = dynamic_cast<GraphNodeItem*>(item);
        if (!node || !canDeleteNode(node->path()))
            continue;

        const SdfPath path = node->path();
        const QString key = QString::fromStdString(path.GetString());
        if (seen.contains(key))
            continue;

        seen.insert(key);
        paths.append(path);
    }

    std::sort(paths.begin(), paths.end(),
              [](const SdfPath& a, const SdfPath& b) { return a.GetString() < b.GetString(); });
    return paths;
}

void
MaterialGraphPrivate::deleteNodes(const QList<SdfPath>& paths)
{
    if (!d.graph || paths.isEmpty())
        return;

    QList<SdfPath> filtered;
    QSet<QString> seen;
    for (const SdfPath& path : paths) {
        if (!canDeleteNode(path))
            continue;

        const QString key = QString::fromStdString(path.GetString());
        if (seen.contains(key))
            continue;

        seen.insert(key);
        filtered.append(path);
    }

    if (filtered.isEmpty())
        return;

    auto commands = std::make_shared<std::vector<Command>>();
    commands->reserve(static_cast<size_t>(filtered.size()));
    for (const SdfPath& path : filtered)
        commands->push_back(deleteShaderNode(path));

    session()->commandStack()->run(new Command(
        [commands](Session* activeSession) {
            for (Command& command : *commands)
                command.execute(activeSession);
        },
        [commands](Session* activeSession) {
            for (auto it = commands->rbegin(); it != commands->rend(); ++it)
                it->undo(activeSession);
        }));
}


bool
MaterialGraphPrivate::canDeleteNode(const SdfPath& path) const
{
    return !path.IsEmpty() && path != d.material.shaderPath;
}

void
MaterialGraphPrivate::createFreeNode(const QString& shaderId, const QString& nodeName, const TfToken& outputName,
                                     const SdfValueTypeName& outputType, const QPointF& scenePos)
{
    if (!d.graph || d.material.materialPath.IsEmpty() || shaderId.isEmpty())
        return;

    // Capture placement before running the command: USD notices may rebuild synchronously.
    d.pendingCreate = true;
    d.pendingCreatePosition = scenePos;
    d.pendingCreateExistingNodes.clear();
    for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it)
        d.pendingCreateExistingNodes.insert(it.key());

    session()->commandStack()->run(
        new Command(newShaderNode(d.material.materialPath, shaderId, nodeName, outputName, outputType)));
}

void
MaterialGraphPrivate::createMaterialXNode(const MaterialXNodeDefinition& definition, const QPointF& scenePos)
{
    if (!d.graph || d.material.materialPath.IsEmpty() || definition.nodeDef.isEmpty())
        return;

    d.pendingCreate = true;
    d.pendingCreatePosition = scenePos;
    d.pendingCreateExistingNodes.clear();
    for (auto it = d.nodes.cbegin(); it != d.nodes.cend(); ++it)
        d.pendingCreateExistingNodes.insert(it.key());

    session()->commandStack()->run(new Command(newMaterialXNode(d.material.materialPath, definition)));
}


void
MaterialGraphPrivate::showCreateNodeMenu(const QPoint& viewportPos)
{
    if (!d.graph || d.material.materialPath.IsEmpty() || !d.view)
        return;

    const QPointF createScenePos = d.view->mapToScene(viewportPos);

    QMenu menu(d.graph.data());
    QMenu* create = menu.addMenu(QObject::tr("Create Node"));

    MaterialMenu::Request request;

    const bool materialX = d.material.shaderId.startsWith(QStringLiteral("ND_"));

    request.materialXFirst = materialX;
    request.includeSearch = true;
    request.materialXOnly = materialX;
    request.usdPreviewOnly = !materialX;
    request.flattenFamily = true;

    MaterialMenu::populate(create, request, d.graph.data(), [this, createScenePos](const MaterialMenu::Choice& choice) {
        if (choice.family == MaterialMenu::Family::MaterialX) {
            createMaterialXNode(choice.materialXDefinition, createScenePos);
        }
        else {
            createFreeNode(choice.shaderId, choice.nodeName, choice.outputName, choice.outputType, createScenePos);
        }
    });

    menu.addSeparator();
    QAction* frameSelected = menu.addAction(QObject::tr("Frame Selected\tF"));
    frameSelected->setEnabled(!d.scene.selectedItems().isEmpty());
    QObject::connect(frameSelected, &QAction::triggered, d.graph.data(), [this]() {
        if (d.graph)
            d.graph->frameSelected();
    });

    QAction* frameAll = menu.addAction(QObject::tr("Frame All\tA"));
    QObject::connect(frameAll, &QAction::triggered, d.graph.data(), [this]() {
        if (d.graph)
            d.graph->frameAll();
    });

    menu.addSeparator();
    QAction* rebuild = menu.addAction(QObject::tr("Rebuild Graph\tR"));
    QObject::connect(rebuild, &QAction::triggered, d.graph.data(), [this]() {
        if (d.graph)
            d.graph->rebuild();
    });

    menu.exec(d.view->viewport()->mapToGlobal(viewportPos));
}

void
MaterialGraphPrivate::zoom(qreal factor)
{
    if (!d.view || !std::isfinite(factor) || factor <= 0.0)
        return;

    constexpr qreal minimumScale = 0.10;
    constexpr qreal maximumScale = 4.00;

    const qreal currentScale = d.view->transform().m11();
    if (!std::isfinite(currentScale) || currentScale <= 0.0)
        return;

    const qreal targetScale = std::clamp(currentScale * factor, minimumScale, maximumScale);
    const qreal appliedFactor = targetScale / currentScale;
    if (std::abs(appliedFactor - 1.0) < 1e-6)
        return;

    d.view->scale(appliedFactor, appliedFactor);
}

void
MaterialGraphPrivate::pan(const QPoint& delta)
{
    if (!d.view || delta.isNull())
        return;

    // Match the viewport's natural trackpad motion: the graph follows the
    // fingers rather than behaving like a traditional mouse wheel.
    d.view->horizontalScrollBar()->setValue(d.view->horizontalScrollBar()->value() - delta.x());
    d.view->verticalScrollBar()->setValue(d.view->verticalScrollBar()->value() - delta.y());
}

void
MaterialGraphPrivate::resetView()
{
    if (!d.view)
        return;

    d.view->resetTransform();

    GraphNodeItem* root = d.nodes.value(QString::fromStdString(d.material.shaderPath.GetString()));
    if (root && root->isVisible()) {
        d.view->centerOn(root->sceneBoundingRect().center());
        return;
    }

    const QRectF visible = visibleItemsRect();
    if (!visible.isEmpty())
        d.view->centerOn(visible.center());
}

MaterialGraph::MaterialGraph(QWidget* parent)
    : QWidget(parent)
    , p(new MaterialGraphPrivate(this))
{
    p->d.ui.reset(new Ui_MaterialGraph());
    p->d.ui->setupUi(this);
    p->d.view = p->d.ui->graph;
    // Scope single-key graph shortcuts to the graph view; never install an app-wide filter.
    p->d.view->installEventFilter(this);
    p->d.view->viewport()->installEventFilter(this);

    setAutoFillBackground(true);
    QPalette pagePalette = palette();
    pagePalette.setColor(QPalette::Window, stageviz::style()->color(Style::ColorRole::Item));
    setPalette(pagePalette);

    p->d.ui->filterBar->setAutoFillBackground(false);
    p->d.ui->filter->setMinimumHeight(30);
    p->d.ui->filter->setClearButtonEnabled(false);
    p->d.ui->clear->setIcon(stageviz::style()->icon(Style::IconRole::Clear));
    p->d.ui->clear->setText(QString());
    p->d.ui->clear->setToolTip(tr("Clear"));
    p->d.ui->clear->setEnabled(false);
    p->d.view->setScene(&p->d.scene);
    p->d.scene.setSceneRect(p->d.canvasRect);
    p->d.view->setBackgroundBrush(Qt::NoBrush);
    p->d.view->setRenderHint(QPainter::Antialiasing, true);
    p->d.view->setDragMode(QGraphicsView::RubberBandDrag);
    p->d.view->setRubberBandSelectionMode(Qt::IntersectsItemShape);
    p->d.view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    p->d.view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    p->d.view->setFrameShape(QFrame::NoFrame);
    p->d.view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    p->d.view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    p->d.view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    p->d.view->setFocusPolicy(Qt::StrongFocus);
    p->d.view->setContextMenuPolicy(Qt::DefaultContextMenu);
    // connect
    connect(p->d.ui->filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        p->applyFilter(text);
        p->d.ui->clear->setEnabled(!text.isEmpty());
    });
    connect(p->d.ui->clear, &QToolButton::clicked, p->d.ui->filter, &QLineEdit::clear);
}

MaterialGraph::~MaterialGraph() = default;

void
MaterialGraph::setMaterial(const MaterialEntry& material)
{
    const bool materialChanged = p->d.material.materialPath != material.materialPath;
    p->d.material = material;
    if (materialChanged) {
        p->d.viewInitialized = false;
        p->d.canvasRect = QRectF(-50000.0, -50000.0, 100000.0, 100000.0);
        p->d.scene.setSceneRect(p->d.canvasRect);
        p->d.pendingCreate = false;
        p->d.pendingCreateExistingNodes.clear();
    }
    p->rebuild();
}

MaterialEntry
MaterialGraph::material() const
{
    return p->d.material;
}

SdfPath
MaterialGraph::materialPath() const
{
    return p->d.material.materialPath;
}

SdfPath
MaterialGraph::selectedNodePath() const
{
    return p->d.selectedNode;
}

QGraphicsView*
MaterialGraph::graphicsView() const
{
    return p->d.view.data();
}

QWidget*
MaterialGraph::viewport() const
{
    return p->d.view ? p->d.view->viewport() : nullptr;
}

void
MaterialGraph::refresh()
{
    p->rebuild();
}

void
MaterialGraph::rebuild()
{
    p->rebuild(false);
    frameAll();
}

void
MaterialGraph::frameAll()
{
    const QRectF rect = p->visibleItemsRect();
    if (rect.isEmpty())
        return;

    QGraphicsView* view = p->d.view.data();
    view->resetTransform();

    const QRectF framed = rect.adjusted(-40.0, -40.0, 40.0, 40.0);
    const QSizeF viewportSize = view->viewport()->size();
    if (viewportSize.width() <= 0.0 || viewportSize.height() <= 0.0 || framed.width() <= 0.0
        || framed.height() <= 0.0) {
        return;
    }

    const qreal scaleX = viewportSize.width() / framed.width();
    const qreal scaleY = viewportSize.height() / framed.height();
    const qreal factor = std::clamp(std::min(scaleX, scaleY), 0.10, 1.0);
    view->scale(factor, factor);
    view->centerOn(rect.center());
}

void
MaterialGraph::frameSelected()
{
    const QRectF rect = p->selectedItemsRect();
    if (rect.isEmpty())
        return;

    QGraphicsView* view = p->d.view.data();
    view->resetTransform();

    const QRectF framed = rect.adjusted(-50.0, -50.0, 50.0, 50.0);
    const QSizeF viewportSize = view->viewport()->size();
    if (viewportSize.width() <= 0.0 || viewportSize.height() <= 0.0 || framed.width() <= 0.0
        || framed.height() <= 0.0) {
        return;
    }

    const qreal scaleX = viewportSize.width() / framed.width();
    const qreal scaleY = viewportSize.height() / framed.height();
    const qreal factor = std::clamp(std::min(scaleX, scaleY), 0.10, 2.0);
    view->scale(factor, factor);
    view->centerOn(rect.center());
}

bool
MaterialGraph::eventFilter(QObject* object, QEvent* event)
{
    if (!event || !p->d.view)
        return QWidget::eventFilter(object, event);

    QGraphicsView* view = p->d.view.data();
    const bool onView = object == view;
    const bool onViewport = object == view->viewport();
    if (!onView && !onViewport)
        return QWidget::eventFilter(object, event);

    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier
            && (key->key() == Qt::Key_A || key->key() == Qt::Key_F || key->key() == Qt::Key_R
                || key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace)) {
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier) {
            if (key->key() == Qt::Key_A) {
                frameAll();
                event->accept();
                return true;
            }
            if (key->key() == Qt::Key_F) {
                frameSelected();
                event->accept();
                return true;
            }
            if (key->key() == Qt::Key_R) {
                rebuild();
                event->accept();
                return true;
            }
            if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
                p->deleteNodes(p->selectedDeletableNodePaths());
                event->accept();
                return true;
            }
        }
    }

#ifdef Q_OS_MAC
    if (event->type() == QEvent::NativeGesture) {
        auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const qreal delta = std::clamp<qreal>(gesture->value(), -0.5, 0.5);
            p->zoom(std::exp(delta));
            event->accept();
            return true;
        }
    }
#endif

    if (onViewport && event->type() == QEvent::ContextMenu) {
        auto* context = static_cast<QContextMenuEvent*>(event);
        if (!view->itemAt(context->pos())) {
            p->showCreateNodeMenu(context->pos());
            event->accept();
            return true;
        }
    }

    if (onViewport && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            p->d.middlePan = true;
            p->d.lastPanPosition = mouse->pos();
            view->viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }

    if (onViewport && event->type() == QEvent::MouseMove && p->d.middlePan) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint position = mouse->pos();
        p->pan(position - p->d.lastPanPosition);
        p->d.lastPanPosition = position;
        event->accept();
        return true;
    }

    if (onViewport && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (p->d.middlePan && mouse->button() == Qt::MiddleButton) {
            p->d.middlePan = false;
            p->d.lastPanPosition = QPoint();
            view->viewport()->unsetCursor();
            event->accept();
            return true;
        }
    }

    if ((onView || onViewport) && event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        const QPoint pixelDelta = wheel->pixelDelta();
        const QPoint angleDelta = wheel->angleDelta();
        const QPointingDevice* device = wheel->pointingDevice();
        const bool isTrackpad = device && device->type() == QInputDevice::DeviceType::TouchPad;

        if (isTrackpad && !pixelDelta.isNull()) {
            if (wheel->modifiers() & Qt::ShiftModifier) {
                const qreal delta = std::clamp<qreal>(static_cast<qreal>(pixelDelta.y()) / 300.0, -0.5, 0.5);
                p->zoom(std::exp(delta));
            }
            else {
                p->pan(pixelDelta);
            }
            event->accept();
            return true;
        }

        QPoint zoomDelta = angleDelta;
        qreal divisor = 600.0;
        if (zoomDelta.isNull() && !pixelDelta.isNull()) {
            zoomDelta = pixelDelta;
            divisor = 300.0;
        }

        if (!zoomDelta.isNull()) {
            const qreal delta = std::clamp<qreal>(static_cast<qreal>(zoomDelta.y()) / divisor, -0.5, 0.5);
            p->zoom(std::exp(delta));
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace stageviz
