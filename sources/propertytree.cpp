// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertytree.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "notice.h"
#include "propertyitem.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "signalguard.h"
#include "tracelocks.h"
#include "viewcontext.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <type_traits>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

constexpr int kArrayChunkSize = 256;

template <typename T>
QString streamText(const T& value)
{
    std::ostringstream ss;
    ss << value;
    return QString::fromStdString(ss.str());
}

QString valueText(const bool& value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString valueText(const std::string& value)
{
    return QString::fromStdString(value);
}

QString valueText(const TfToken& value)
{
    return QString::fromStdString(value.GetString());
}

QString valueText(const SdfAssetPath& value)
{
    return QString::fromStdString(value.GetAssetPath());
}

template <typename T>
QString valueText(const T& value)
{
    return streamText(value);
}

QString cleanNumericText(QString text)
{
    text = text.trimmed();
    text.remove('(');
    text.remove(')');
    text.remove('[');
    text.remove(']');
    text.replace(',', ' ');
    text.replace(';', ' ');
    return text.simplified();
}

QStringList numericTokens(const QString& text)
{
    return cleanNumericText(text).split(' ', Qt::SkipEmptyParts);
}

template <typename T>
bool parseIntegral(const QString& text, T& result, QString& error)
{
    bool ok = false;

    if constexpr (std::is_signed_v<T>) {
        const qlonglong value = text.trimmed().toLongLong(&ok);
        if (ok && value >= static_cast<qlonglong>(std::numeric_limits<T>::min())
            && value <= static_cast<qlonglong>(std::numeric_limits<T>::max())) {
            result = static_cast<T>(value);
            return true;
        }
    }
    else {
        const qulonglong value = text.trimmed().toULongLong(&ok);
        if (ok && value <= static_cast<qulonglong>(std::numeric_limits<T>::max())) {
            result = static_cast<T>(value);
            return true;
        }
    }

    error = QStringLiteral("Expected an integer value");
    return false;
}

template <typename T>
bool parseFloating(const QString& text, T& result, QString& error)
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok) {
        error = QStringLiteral("Expected a numeric value");
        return false;
    }
    result = static_cast<T>(value);
    return true;
}

bool parseValue(const QString& text, bool& result, QString& error)
{
    const QString value = text.trimmed().toLower();
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        result = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        result = false;
        return true;
    }
    error = QStringLiteral("Expected true or false");
    return false;
}

bool parseValue(const QString& text, int& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool parseValue(const QString& text, unsigned int& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool parseValue(const QString& text, int64_t& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool parseValue(const QString& text, uint64_t& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool parseValue(const QString& text, float& result, QString& error)
{
    return parseFloating(text, result, error);
}

bool parseValue(const QString& text, double& result, QString& error)
{
    return parseFloating(text, result, error);
}

bool parseValue(const QString& text, std::string& result, QString&)
{
    result = text.toStdString();
    return true;
}

bool parseValue(const QString& text, TfToken& result, QString&)
{
    result = TfToken(text.trimmed().toStdString());
    return true;
}

bool parseValue(const QString& text, SdfAssetPath& result, QString&)
{
    result = SdfAssetPath(text.trimmed().toStdString());
    return true;
}

template <typename T, int N>
bool parseVector(const QString& text, T& result, QString& error)
{
    const QStringList parts = numericTokens(text);
    if (parts.size() != N) {
        error = QString("Expected %1 components").arg(N);
        return false;
    }

    using Scalar = typename T::ScalarType;
    for (int i = 0; i < N; ++i) {
        Scalar value {};
        if constexpr (std::is_integral_v<Scalar>) {
            if (!parseIntegral(parts[i], value, error))
                return false;
        }
        else {
            if (!parseFloating(parts[i], value, error))
                return false;
        }
        result[i] = value;
    }
    return true;
}

bool parseValue(const QString& text, GfVec2i& result, QString& error) { return parseVector<GfVec2i, 2>(text, result, error); }
bool parseValue(const QString& text, GfVec3i& result, QString& error) { return parseVector<GfVec3i, 3>(text, result, error); }
bool parseValue(const QString& text, GfVec4i& result, QString& error) { return parseVector<GfVec4i, 4>(text, result, error); }
bool parseValue(const QString& text, GfVec2f& result, QString& error) { return parseVector<GfVec2f, 2>(text, result, error); }
bool parseValue(const QString& text, GfVec3f& result, QString& error) { return parseVector<GfVec3f, 3>(text, result, error); }
bool parseValue(const QString& text, GfVec4f& result, QString& error) { return parseVector<GfVec4f, 4>(text, result, error); }
bool parseValue(const QString& text, GfVec2d& result, QString& error) { return parseVector<GfVec2d, 2>(text, result, error); }
bool parseValue(const QString& text, GfVec3d& result, QString& error) { return parseVector<GfVec3d, 3>(text, result, error); }
bool parseValue(const QString& text, GfVec4d& result, QString& error) { return parseVector<GfVec4d, 4>(text, result, error); }

bool parseValue(const QString& text, GfQuatf& result, QString& error)
{
    GfVec4f values;
    if (!parseVector<GfVec4f, 4>(text, values, error))
        return false;
    result = GfQuatf(values[0], GfVec3f(values[1], values[2], values[3]));
    return true;
}

bool parseValue(const QString& text, GfQuatd& result, QString& error)
{
    GfVec4d values;
    if (!parseVector<GfVec4d, 4>(text, values, error))
        return false;
    result = GfQuatd(values[0], GfVec3d(values[1], values[2], values[3]));
    return true;
}

QString valueText(const GfQuatf& value)
{
    const GfVec3f i = value.GetImaginary();
    return QString("(%1, %2, %3, %4)")
        .arg(value.GetReal()).arg(i[0]).arg(i[1]).arg(i[2]);
}

QString valueText(const GfQuatd& value)
{
    const GfVec3d i = value.GetImaginary();
    return QString("(%1, %2, %3, %4)")
        .arg(value.GetReal()).arg(i[0]).arg(i[1]).arg(i[2]);
}

template <typename T, int N>
bool parseMatrix(const QString& text, T& result, QString& error)
{
    const QStringList parts = numericTokens(text);
    if (parts.size() != N * N) {
        error = QString("Expected %1 matrix values").arg(N * N);
        return false;
    }

    int index = 0;
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            bool ok = false;
            const double value = parts[index++].toDouble(&ok);
            if (!ok) {
                error = QStringLiteral("Expected numeric matrix values");
                return false;
            }
            result[r][c] = value;
        }
    }
    return true;
}

bool parseValue(const QString& text, GfMatrix2d& result, QString& error) { return parseMatrix<GfMatrix2d, 2>(text, result, error); }
bool parseValue(const QString& text, GfMatrix3d& result, QString& error) { return parseMatrix<GfMatrix3d, 3>(text, result, error); }
bool parseValue(const QString& text, GfMatrix4d& result, QString& error) { return parseMatrix<GfMatrix4d, 4>(text, result, error); }

template <typename T>
bool tryScalarParse(const VtValue& current, const QString& text, VtValue& result, QString& error)
{
    if (!current.IsHolding<T>())
        return false;

    T value {};
    if (!parseValue(text, value, error))
        return true;

    result = VtValue(value);
    return true;
}

bool parseScalar(const VtValue& current, const QString& text, VtValue& result, QString& error)
{
#define TRY_SCALAR(TYPE) \
    if (current.IsHolding<TYPE>()) { \
        TYPE value {}; \
        if (!parseValue(text, value, error)) \
            return false; \
        result = VtValue(value); \
        return true; \
    }

    TRY_SCALAR(bool)
    TRY_SCALAR(int)
    TRY_SCALAR(unsigned int)
    TRY_SCALAR(int64_t)
    TRY_SCALAR(uint64_t)
    TRY_SCALAR(float)
    TRY_SCALAR(double)
    TRY_SCALAR(std::string)
    TRY_SCALAR(TfToken)
    TRY_SCALAR(SdfAssetPath)
    TRY_SCALAR(GfVec2i)
    TRY_SCALAR(GfVec3i)
    TRY_SCALAR(GfVec4i)
    TRY_SCALAR(GfVec2f)
    TRY_SCALAR(GfVec3f)
    TRY_SCALAR(GfVec4f)
    TRY_SCALAR(GfVec2d)
    TRY_SCALAR(GfVec3d)
    TRY_SCALAR(GfVec4d)
    TRY_SCALAR(GfQuatf)
    TRY_SCALAR(GfQuatd)
    TRY_SCALAR(GfMatrix2d)
    TRY_SCALAR(GfMatrix3d)
    TRY_SCALAR(GfMatrix4d)

#undef TRY_SCALAR

    error = QString("Unsupported editable type: %1").arg(QString::fromStdString(current.GetTypeName()));
    return false;
}

template <typename T>
bool replaceArrayElementTyped(const VtValue& current, int index, const QString& text, VtValue& result, QString& error)
{
    if (!current.IsHolding<VtArray<T>>())
        return false;

    VtArray<T> array = current.UncheckedGet<VtArray<T>>();
    if (index < 0 || index >= static_cast<int>(array.size())) {
        error = QStringLiteral("Array index is out of range");
        return true;
    }

    T value {};
    if (!parseValue(text, value, error))
        return true;

    array[index] = value;
    result = VtValue(array);
    return true;
}

bool replaceArrayElement(const VtValue& current, int index, const QString& text, VtValue& result, QString& error)
{
#define TRY_ARRAY(TYPE) \
    if (current.IsHolding<VtArray<TYPE>>()) { \
        VtArray<TYPE> array = current.UncheckedGet<VtArray<TYPE>>(); \
        if (index < 0 || index >= static_cast<int>(array.size())) { \
            error = QStringLiteral("Array index is out of range"); \
            return false; \
        } \
        TYPE value {}; \
        if (!parseValue(text, value, error)) \
            return false; \
        array[index] = value; \
        result = VtValue(array); \
        return true; \
    }

    TRY_ARRAY(bool)
    TRY_ARRAY(int)
    TRY_ARRAY(unsigned int)
    TRY_ARRAY(int64_t)
    TRY_ARRAY(uint64_t)
    TRY_ARRAY(float)
    TRY_ARRAY(double)
    TRY_ARRAY(std::string)
    TRY_ARRAY(TfToken)
    TRY_ARRAY(SdfAssetPath)
    TRY_ARRAY(GfVec2i)
    TRY_ARRAY(GfVec3i)
    TRY_ARRAY(GfVec4i)
    TRY_ARRAY(GfVec2f)
    TRY_ARRAY(GfVec3f)
    TRY_ARRAY(GfVec4f)
    TRY_ARRAY(GfVec2d)
    TRY_ARRAY(GfVec3d)
    TRY_ARRAY(GfVec4d)
    TRY_ARRAY(GfQuatf)
    TRY_ARRAY(GfQuatd)
    TRY_ARRAY(GfMatrix2d)
    TRY_ARRAY(GfMatrix3d)
    TRY_ARRAY(GfMatrix4d)

#undef TRY_ARRAY

    error = QString("Unsupported array type: %1").arg(QString::fromStdString(current.GetTypeName()));
    return false;
}

template <typename T>
bool arrayInfoTyped(const VtValue& value, int& size)
{
    if (!value.IsHolding<VtArray<T>>())
        return false;
    size = static_cast<int>(value.UncheckedGet<VtArray<T>>().size());
    return true;
}

bool arrayInfo(const VtValue& value, int& size)
{
#define ARRAY_INFO(TYPE) if (arrayInfoTyped<TYPE>(value, size)) return true;
    ARRAY_INFO(bool)
    ARRAY_INFO(int)
    ARRAY_INFO(unsigned int)
    ARRAY_INFO(int64_t)
    ARRAY_INFO(uint64_t)
    ARRAY_INFO(float)
    ARRAY_INFO(double)
    ARRAY_INFO(std::string)
    ARRAY_INFO(TfToken)
    ARRAY_INFO(SdfAssetPath)
    ARRAY_INFO(GfVec2i)
    ARRAY_INFO(GfVec3i)
    ARRAY_INFO(GfVec4i)
    ARRAY_INFO(GfVec2f)
    ARRAY_INFO(GfVec3f)
    ARRAY_INFO(GfVec4f)
    ARRAY_INFO(GfVec2d)
    ARRAY_INFO(GfVec3d)
    ARRAY_INFO(GfVec4d)
    ARRAY_INFO(GfQuatf)
    ARRAY_INFO(GfQuatd)
    ARRAY_INFO(GfMatrix2d)
    ARRAY_INFO(GfMatrix3d)
    ARRAY_INFO(GfMatrix4d)
#undef ARRAY_INFO
    return false;
}

template <typename T>
bool arrayElementTextTyped(const VtValue& value, int index, QString& text)
{
    if (!value.IsHolding<VtArray<T>>())
        return false;

    const VtArray<T>& array = value.UncheckedGet<VtArray<T>>();
    if (index < 0 || index >= static_cast<int>(array.size()))
        return false;

    text = valueText(array[index]);
    return true;
}

bool arrayElementText(const VtValue& value, int index, QString& text)
{
#define ARRAY_TEXT(TYPE) if (arrayElementTextTyped<TYPE>(value, index, text)) return true;
    ARRAY_TEXT(bool)
    ARRAY_TEXT(int)
    ARRAY_TEXT(unsigned int)
    ARRAY_TEXT(int64_t)
    ARRAY_TEXT(uint64_t)
    ARRAY_TEXT(float)
    ARRAY_TEXT(double)
    ARRAY_TEXT(std::string)
    ARRAY_TEXT(TfToken)
    ARRAY_TEXT(SdfAssetPath)
    ARRAY_TEXT(GfVec2i)
    ARRAY_TEXT(GfVec3i)
    ARRAY_TEXT(GfVec4i)
    ARRAY_TEXT(GfVec2f)
    ARRAY_TEXT(GfVec3f)
    ARRAY_TEXT(GfVec4f)
    ARRAY_TEXT(GfVec2d)
    ARRAY_TEXT(GfVec3d)
    ARRAY_TEXT(GfVec4d)
    ARRAY_TEXT(GfQuatf)
    ARRAY_TEXT(GfQuatd)
    ARRAY_TEXT(GfMatrix2d)
    ARRAY_TEXT(GfMatrix3d)
    ARRAY_TEXT(GfMatrix4d)
#undef ARRAY_TEXT
    return false;
}

QString scalarText(const VtValue& value)
{
#define SCALAR_TEXT(TYPE) if (value.IsHolding<TYPE>()) return valueText(value.UncheckedGet<TYPE>());
    SCALAR_TEXT(bool)
    SCALAR_TEXT(int)
    SCALAR_TEXT(unsigned int)
    SCALAR_TEXT(int64_t)
    SCALAR_TEXT(uint64_t)
    SCALAR_TEXT(float)
    SCALAR_TEXT(double)
    SCALAR_TEXT(std::string)
    SCALAR_TEXT(TfToken)
    SCALAR_TEXT(SdfAssetPath)
    SCALAR_TEXT(GfVec2i)
    SCALAR_TEXT(GfVec3i)
    SCALAR_TEXT(GfVec4i)
    SCALAR_TEXT(GfVec2f)
    SCALAR_TEXT(GfVec3f)
    SCALAR_TEXT(GfVec4f)
    SCALAR_TEXT(GfVec2d)
    SCALAR_TEXT(GfVec3d)
    SCALAR_TEXT(GfVec4d)
    SCALAR_TEXT(GfQuatf)
    SCALAR_TEXT(GfQuatd)
    SCALAR_TEXT(GfMatrix2d)
    SCALAR_TEXT(GfMatrix3d)
    SCALAR_TEXT(GfMatrix4d)
#undef SCALAR_TEXT
    return QString::fromStdString(value.GetTypeName());
}

bool scalarEditable(const VtValue& value)
{
#define IS_SCALAR(TYPE) if (value.IsHolding<TYPE>()) return true;
    IS_SCALAR(bool)
    IS_SCALAR(int)
    IS_SCALAR(unsigned int)
    IS_SCALAR(int64_t)
    IS_SCALAR(uint64_t)
    IS_SCALAR(float)
    IS_SCALAR(double)
    IS_SCALAR(std::string)
    IS_SCALAR(TfToken)
    IS_SCALAR(SdfAssetPath)
    IS_SCALAR(GfVec2i)
    IS_SCALAR(GfVec3i)
    IS_SCALAR(GfVec4i)
    IS_SCALAR(GfVec2f)
    IS_SCALAR(GfVec3f)
    IS_SCALAR(GfVec4f)
    IS_SCALAR(GfVec2d)
    IS_SCALAR(GfVec3d)
    IS_SCALAR(GfVec4d)
    IS_SCALAR(GfQuatf)
    IS_SCALAR(GfQuatd)
    IS_SCALAR(GfMatrix2d)
    IS_SCALAR(GfMatrix3d)
    IS_SCALAR(GfMatrix4d)
#undef IS_SCALAR
    return false;
}

}  // namespace

class PropertyTreePrivate : public QObject, public SignalGuard {
public:
    void init();
    void close();
    void updateStage(UsdStageRefPtr stage);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);

    void addAttribute(PropertyItem* parent, const UsdAttribute& attr);
    void addArrayElements(PropertyItem* parent, const SdfPath& propertyPath, const VtValue& value,
                          int start, int count);
    void populateChunk(PropertyItem* item);
    void itemChanged(QTreeWidgetItem* item, int column);
    void itemExpanded(QTreeWidgetItem* item);
    void restoreItemText(PropertyItem* item);

    bool currentAttributeValue(const SdfPath& propertyPath, VtValue& value) const;

    struct Data {
        UsdStageRefPtr stage;
        SdfPath path;
        QPointer<ViewContext> context;
        QPointer<PropertyTree> tree;
        bool updating = false;
    };
    Data d;
};

void
PropertyTreePrivate::init()
{
    attach(d.tree);

    connect(d.tree.data(), &QTreeWidget::itemChanged, this, &PropertyTreePrivate::itemChanged);
    connect(d.tree.data(), &QTreeWidget::itemExpanded, this, &PropertyTreePrivate::itemExpanded);
}

void
PropertyTreePrivate::close()
{
    QSignalBlocker blocker(d.tree.data());
    d.updating = true;
    d.stage = nullptr;
    d.path = SdfPath();
    d.tree->clear();
    d.updating = false;
}

void
PropertyTreePrivate::updateStage(UsdStageRefPtr stage)
{
    QSignalBlocker blocker(d.tree.data());
    d.updating = true;

    d.tree->clear();
    d.stage = stage;
    d.path = SdfPath();

    if (!stage) {
        d.updating = false;
        return;
    }

    PropertyItem* stageItem = new PropertyItem(d.tree.data());
    stageItem->setKind(PropertyItem::Group);
    stageItem->setText(PropertyItem::Name, "Stage");
    stageItem->setExpanded(true);

    auto addChild = [&](const QString& name, const QString& value) {
        PropertyItem* item = new PropertyItem(stageItem);
        item->setKind(PropertyItem::Group);
        item->setText(PropertyItem::Name, name);
        item->setText(PropertyItem::Value, value);
    };

    addChild("metersPerUnit", QString::number(UsdGeomGetStageMetersPerUnit(stage)));
    addChild("upAxis", StringToQString(UsdGeomGetStageUpAxis(stage).GetString()));
    addChild("timeCodesPerSecond", QString::number(stage->GetTimeCodesPerSecond()));
    addChild("startTimeCode", QString::number(stage->GetStartTimeCode()));
    addChild("endTimeCode", QString::number(stage->GetEndTimeCode()));

    const std::string comment = stage->GetRootLayer()->GetComment();
    if (!comment.empty())
        addChild("comment", StringToQString(comment));

    const std::string filePath = stage->GetRootLayer()->GetRealPath();
    addChild("filePath", QFileInfo(StringToQString(filePath)).fileName());

    d.updating = false;
}

void
PropertyTreePrivate::updatePrims(const NoticeBatch& batch)
{
    if (d.path.IsEmpty() || batch.entries.isEmpty())
        return;

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        const SdfPath entryPath = entry.path.IsPropertyPath() ? entry.path.GetPrimPath() : entry.path;

        if (entry.changedInfoOnly) {
            if (entryPath == d.path) {
                updateSelection({ d.path });
                return;
            }
            continue;
        }

        if (entry.resolvedAssetPathsResynced) {
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({ d.path });
                return;
            }
            continue;
        }

        switch (entry.primResyncType) {
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentDestination:
            if (!entry.associatedPath.IsEmpty() && d.path == entry.associatedPath) {
                updateSelection({ entryPath });
                return;
            }
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({ d.path });
                return;
            }
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::RenameSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentSource:
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                if (!entry.associatedPath.IsEmpty()) {
                    updateSelection({ entry.associatedPath });
                    return;
                }
                updateSelection({});
                return;
            }
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::Delete:
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({});
                return;
            }
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::UnchangedPrimStack:
        case UsdNotice::ObjectsChanged::PrimResyncType::Other:
        case UsdNotice::ObjectsChanged::PrimResyncType::Invalid:
        default:
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({ d.path });
                return;
            }
            break;
        }
    }
}

void
PropertyTreePrivate::addArrayElements(PropertyItem* parent, const SdfPath& propertyPath,
                                      const VtValue& value, int start, int count)
{
    const int end = start + count;
    for (int index = start; index < end; ++index) {
        QString text;
        if (!arrayElementText(value, index, text))
            continue;

        PropertyItem* child = new PropertyItem(parent);
        child->setKind(PropertyItem::ArrayElement);
        child->setPropertyPath(propertyPath);
        child->setArrayIndex(index);
        child->setText(PropertyItem::Name, QString("[%1]").arg(index));
        child->setText(PropertyItem::Value, text);
        child->setValueEditable(true);
    }
}

void
PropertyTreePrivate::addAttribute(PropertyItem* parent, const UsdAttribute& attr)
{
    PropertyItem* item = new PropertyItem(parent);
    item->setKind(PropertyItem::Attribute);
    item->setPropertyPath(attr.GetPath());
    item->setText(PropertyItem::Name, StringToQString(attr.GetName().GetString()));
    item->setToolTip(PropertyItem::Name, QString::fromStdString(attr.GetTypeName().GetAsToken().GetString()));

    VtValue value;
    if (!attr.Get(&value)) {
        item->setText(PropertyItem::Value, "<no default>");
        item->setToolTip(PropertyItem::Value, "No composed default value");
        return;
    }

    int arraySize = 0;
    if (arrayInfo(value, arraySize)) {
        item->setText(PropertyItem::Value, QString("%1 values").arg(arraySize));
        item->setToolTip(PropertyItem::Value, QString::fromStdString(value.GetTypeName()));

        if (arraySize <= kArrayChunkSize) {
            addArrayElements(item, attr.GetPath(), value, 0, arraySize);
        }
        else {
            for (int start = 0; start < arraySize; start += kArrayChunkSize) {
                const int count = std::min(kArrayChunkSize, arraySize - start);
                PropertyItem* chunk = new PropertyItem(item);
                chunk->setKind(PropertyItem::ArrayChunk);
                chunk->setPropertyPath(attr.GetPath());
                chunk->setChunkRange(start, count);
                chunk->setText(PropertyItem::Name,
                               QString("[%1..%2]").arg(start).arg(start + count - 1));
                chunk->setText(PropertyItem::Value, QString("%1 values").arg(count));

                // Dummy child gives the chunk an expansion arrow. It is replaced lazily.
                PropertyItem* dummy = new PropertyItem(chunk);
                dummy->setKind(PropertyItem::Group);
                dummy->setText(PropertyItem::Name, "...");
            }
        }
        return;
    }

    item->setText(PropertyItem::Value, scalarText(value));
    item->setToolTip(PropertyItem::Value, QString::fromStdString(value.GetTypeName()));
    item->setValueEditable(scalarEditable(value));
}

void
PropertyTreePrivate::updateSelection(const QList<SdfPath>& paths)
{
    QSignalBlocker blocker(d.tree.data());
    d.updating = true;
    d.tree->clear();

    if (!paths.isEmpty()) {
        if (paths.size() > 1) {
            PropertyItem* multiItem = new PropertyItem(d.tree.data());
            multiItem->setKind(PropertyItem::Group);
            multiItem->setText(PropertyItem::Name, "[Multiple selection]");
            multiItem->setExpanded(true);
            d.path = SdfPath();
            d.updating = false;
            return;
        }

        const SdfPath path = paths.first();
        if (!d.stage) {
            d.updating = false;
            return;
        }

        UsdPrim prim;
        {
            READ_LOCKER(locker, d.context ? d.context->stageLock() : session()->stageLock(), "stageLock");
            prim = d.stage->GetPrimAtPath(path);
        }

        if (!prim) {
            d.updating = false;
            return;
        }

        PropertyItem* primItem = new PropertyItem(d.tree.data());
        primItem->setKind(PropertyItem::Group);
        primItem->setText(PropertyItem::Name, StringToQString(path.GetString()));
        primItem->setExpanded(true);

        {
            READ_LOCKER(locker, d.context ? d.context->stageLock() : session()->stageLock(), "stageLock");
            for (const UsdAttribute& attr : prim.GetAttributes())
                addAttribute(primItem, attr);
        }

        d.path = path;
        d.updating = false;
        return;
    }

    d.path = SdfPath();
    d.updating = false;

    if (d.stage)
        updateStage(d.stage);
}

bool
PropertyTreePrivate::currentAttributeValue(const SdfPath& propertyPath, VtValue& value) const
{
    if (!d.stage || propertyPath.IsEmpty())
        return false;

    READ_LOCKER(locker, d.context ? d.context->stageLock() : session()->stageLock(), "stageLock");

    const UsdAttribute attr = d.stage->GetAttributeAtPath(propertyPath);
    if (!attr)
        return false;

    return attr.Get(&value);
}

void
PropertyTreePrivate::populateChunk(PropertyItem* item)
{
    if (!item || item->kind() != PropertyItem::ArrayChunk || item->chunkPopulated())
        return;

    VtValue value;
    if (!currentAttributeValue(item->propertyPath(), value))
        return;

    QSignalBlocker blocker(d.tree.data());
    d.updating = true;

    while (item->childCount() > 0)
        delete item->takeChild(0);

    addArrayElements(item, item->propertyPath(), value, item->chunkStart(), item->chunkCount());
    item->setChunkPopulated(true);

    d.updating = false;
}

void
PropertyTreePrivate::itemExpanded(QTreeWidgetItem* baseItem)
{
    if (d.updating)
        return;

    auto* item = dynamic_cast<PropertyItem*>(baseItem);
    if (!item)
        return;

    populateChunk(item);
}

void
PropertyTreePrivate::restoreItemText(PropertyItem* item)
{
    if (!item)
        return;

    VtValue value;
    if (!currentAttributeValue(item->propertyPath(), value))
        return;

    QString text;
    if (item->kind() == PropertyItem::ArrayElement) {
        if (!arrayElementText(value, item->arrayIndex(), text))
            return;
    }
    else {
        text = scalarText(value);
    }

    QSignalBlocker blocker(d.tree.data());
    item->setText(PropertyItem::Value, text);
}

void
PropertyTreePrivate::itemChanged(QTreeWidgetItem* baseItem, int column)
{
    if (d.updating || column != PropertyItem::Value)
        return;

    auto* item = dynamic_cast<PropertyItem*>(baseItem);
    if (!item || !item->valueEditable() || item->propertyPath().IsEmpty())
        return;

    VtValue current;
    if (!currentAttributeValue(item->propertyPath(), current)) {
        restoreItemText(item);
        return;
    }

    VtValue updated;
    QString error;
    bool parsed = false;

    if (item->kind() == PropertyItem::ArrayElement)
        parsed = replaceArrayElement(current, item->arrayIndex(), item->text(PropertyItem::Value), updated, error);
    else if (item->kind() == PropertyItem::Attribute)
        parsed = parseScalar(current, item->text(PropertyItem::Value), updated, error);

    if (!parsed || updated.IsEmpty()) {
        restoreItemText(item);
        QMessageBox::warning(d.tree.data(), tr("Invalid property value"),
                             error.isEmpty() ? tr("The value could not be converted to the USD attribute type.")
                                             : error);
        return;
    }

    session()->commandStack()->run(new Command(setAttributeValue(item->propertyPath(), updated)));
}

PropertyTree::PropertyTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new PropertyTreePrivate())
{
    p->d.tree = this;
    p->init();

    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                    | QAbstractItemView::SelectedClicked);
}

PropertyTree::~PropertyTree() = default;

ViewContext*
PropertyTree::context() const
{
    return p->d.context;
}

void
PropertyTree::setContext(ViewContext* context)
{
    p->d.context = context;
}

void
PropertyTree::close()
{
    p->close();
}

void
PropertyTree::updateStage(UsdStageRefPtr stage)
{
    p->updateStage(stage);
}

void
PropertyTree::updatePrims(const NoticeBatch& batch)
{
    p->updatePrims(batch);
}

void
PropertyTree::updateSelection(const QList<SdfPath>& paths)
{
    p->updateSelection(paths);
}

}  // namespace stageviz
