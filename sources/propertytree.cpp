// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertytree.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "messagedialog.h"
#include "notice.h"
#include "propertydelegate.h"
#include "propertyitem.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "signalguard.h"
#include "style.h"
#include "tracelocks.h"
#include "viewcontext.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>
#include <limits>
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
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <sstream>
#include <type_traits>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {
    constexpr int OverrideRole = Qt::UserRole + 100;
}

class PropertyTreePrivate : public QObject, public SignalGuard {
public:
    void init();
    void close();
    void updateStage(UsdStageRefPtr stage);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);

    template<typename T> static QString streamText(const T& value);
    static QString valueText(const bool& value);
    static QString valueText(const std::string& value);
    static QString valueText(const TfToken& value);
    static QString valueText(const SdfAssetPath& value);

    template<typename T> static QString valueText(const T& value);
    static QString valueText(const GfQuatf& value);
    static QString valueText(const GfQuatd& value);

    static QString cleanNumericText(QString text);
    static QStringList numericTokens(const QString& text);

    template<typename T> static bool parseIntegral(const QString& text, T& result, QString& error);
    template<typename T> static bool parseFloating(const QString& text, T& result, QString& error);

    static bool parseValue(const QString& text, bool& result, QString& error);
    static bool parseValue(const QString& text, int& result, QString& error);
    static bool parseValue(const QString& text, unsigned int& result, QString& error);
    static bool parseValue(const QString& text, int64_t& result, QString& error);
    static bool parseValue(const QString& text, uint64_t& result, QString& error);
    static bool parseValue(const QString& text, float& result, QString& error);
    static bool parseValue(const QString& text, double& result, QString& error);
    static bool parseValue(const QString& text, std::string& result, QString& error);
    static bool parseValue(const QString& text, TfToken& result, QString& error);
    static bool parseValue(const QString& text, SdfAssetPath& result, QString& error);

    template<typename T, int N> static bool parseVector(const QString& text, T& result, QString& error);
    static bool parseValue(const QString& text, GfVec2i& result, QString& error);
    static bool parseValue(const QString& text, GfVec3i& result, QString& error);
    static bool parseValue(const QString& text, GfVec4i& result, QString& error);
    static bool parseValue(const QString& text, GfVec2f& result, QString& error);
    static bool parseValue(const QString& text, GfVec3f& result, QString& error);
    static bool parseValue(const QString& text, GfVec4f& result, QString& error);
    static bool parseValue(const QString& text, GfVec2d& result, QString& error);
    static bool parseValue(const QString& text, GfVec3d& result, QString& error);
    static bool parseValue(const QString& text, GfVec4d& result, QString& error);
    static bool parseValue(const QString& text, GfQuatf& result, QString& error);
    static bool parseValue(const QString& text, GfQuatd& result, QString& error);

    template<typename T, int N> static bool parseMatrix(const QString& text, T& result, QString& error);
    static bool parseValue(const QString& text, GfMatrix2d& result, QString& error);
    static bool parseValue(const QString& text, GfMatrix3d& result, QString& error);
    static bool parseValue(const QString& text, GfMatrix4d& result, QString& error);

    template<typename T>
    static bool tryScalarParse(const VtValue& current, const QString& text, VtValue& result, QString& error);
    static bool parseScalar(const VtValue& current, const QString& text, VtValue& result, QString& error);

    template<typename T>
    static bool replaceArrayElementTyped(const VtValue& current, int index, const QString& text, VtValue& result,
                                         QString& error);
    static bool replaceArrayElement(const VtValue& current, int index, const QString& text, VtValue& result,
                                    QString& error);

    template<typename T> static bool arrayInfoTyped(const VtValue& value, int& size);
    static bool arrayInfo(const VtValue& value, int& size);

    template<typename T> static bool arrayElementTextTyped(const VtValue& value, int index, QString& text);
    static bool arrayElementText(const VtValue& value, int index, QString& text);
    static QString scalarText(const VtValue& value);
    static bool scalarEditable(const VtValue& value);

    static QString attributeBaseName(const UsdAttribute& attr);
    static QStringList tokenOptions(const UsdAttribute& attr);
    static PropertyItem::Editor editorForValue(const UsdAttribute& attr, const VtValue& value);
    static void configureEditor(PropertyItem* item, const UsdAttribute& attr, const VtValue& value);
    static bool hasUnderlyingPrimOpinion(const UsdPrim& prim, const SdfLayerHandle& rootLayer);
    bool isOverrideItem(const PropertyItem* item) const;
    QRect overrideIconRect(const PropertyItem* item) const;

    struct TreeState {
        QSet<QString> expanded;
        QString current;
        int scrollValue = 0;
    };

    QString itemKey(const PropertyItem* item) const;
    TreeState captureTreeState() const;
    void restoreTreeState(const TreeState& state);

    PropertyItem* addSection(const QString& name, const QString& value = QString());
    PropertyItem* addInfo(PropertyItem* parent, const QString& name, const QString& value,
                          const QString& toolTip = QString());
    void setReadOnlyValueStyle(PropertyItem* item, bool readOnly = true);
    void addPrimSection(const UsdPrim& prim);
    void addCompositionSection(const UsdPrim& prim);
    void addAttributesSection(const UsdPrim& prim);
    void addRelationshipsSection(const UsdPrim& prim);
    QString payloadAncestorPath(const UsdPrim& prim) const;
    static QString metadataText(const VtValue& value);

    void addAttribute(PropertyItem* parent, const UsdAttribute& attr);
    void addArrayElements(PropertyItem* parent, const SdfPath& propertyPath, const VtValue& value, int start,
                          int count);
    void populateChunk(PropertyItem* item);
    void itemChanged(QTreeWidgetItem* item, int column);
    void itemExpanded(QTreeWidgetItem* item);
    void restoreItemText(PropertyItem* item);
    bool currentAttributeValue(const SdfPath& propertyPath, VtValue& value) const;

public:
    struct Data {
        int chunkSize = 256;
        bool update = false;
        SdfPath path;
        UsdStageRefPtr stage;
        QPointer<ViewContext> context;
        QPointer<PropertyTree> tree;
    };
    Data d;
};

template<typename T>
QString
PropertyTreePrivate::streamText(const T& value)
{
    std::ostringstream ss;
    ss << value;
    return QString::fromStdString(ss.str());
}

QString
PropertyTreePrivate::valueText(const bool& value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString
PropertyTreePrivate::valueText(const std::string& value)
{
    return QString::fromStdString(value);
}

QString
PropertyTreePrivate::valueText(const TfToken& value)
{
    return QString::fromStdString(value.GetString());
}

QString
PropertyTreePrivate::valueText(const SdfAssetPath& value)
{
    return QString::fromStdString(value.GetAssetPath());
}

template<typename T>
QString
PropertyTreePrivate::valueText(const T& value)
{
    return streamText(value);
}

QString
PropertyTreePrivate::cleanNumericText(QString text)
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

QStringList
PropertyTreePrivate::numericTokens(const QString& text)
{
    return cleanNumericText(text).split(' ', Qt::SkipEmptyParts);
}

template<typename T>
bool
PropertyTreePrivate::parseIntegral(const QString& text, T& result, QString& error)
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

template<typename T>
bool
PropertyTreePrivate::parseFloating(const QString& text, T& result, QString& error)
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

bool
PropertyTreePrivate::parseValue(const QString& text, bool& result, QString& error)
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

bool
PropertyTreePrivate::parseValue(const QString& text, int& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, unsigned int& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, int64_t& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, uint64_t& result, QString& error)
{
    return parseIntegral(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, float& result, QString& error)
{
    return parseFloating(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, double& result, QString& error)
{
    return parseFloating(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, std::string& result, QString&)
{
    result = text.toStdString();
    return true;
}

bool
PropertyTreePrivate::parseValue(const QString& text, TfToken& result, QString&)
{
    result = TfToken(text.trimmed().toStdString());
    return true;
}

bool
PropertyTreePrivate::parseValue(const QString& text, SdfAssetPath& result, QString&)
{
    result = SdfAssetPath(text.trimmed().toStdString());
    return true;
}

template<typename T, int N>
bool
PropertyTreePrivate::parseVector(const QString& text, T& result, QString& error)
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

bool
PropertyTreePrivate::parseValue(const QString& text, GfVec2i& result, QString& error)
{
    return parseVector<GfVec2i, 2>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec3i& result, QString& error)
{
    return parseVector<GfVec3i, 3>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec4i& result, QString& error)
{
    return parseVector<GfVec4i, 4>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec2f& result, QString& error)
{
    return parseVector<GfVec2f, 2>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec3f& result, QString& error)
{
    return parseVector<GfVec3f, 3>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec4f& result, QString& error)
{
    return parseVector<GfVec4f, 4>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec2d& result, QString& error)
{
    return parseVector<GfVec2d, 2>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec3d& result, QString& error)
{
    return parseVector<GfVec3d, 3>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfVec4d& result, QString& error)
{
    return parseVector<GfVec4d, 4>(text, result, error);
}

bool
PropertyTreePrivate::parseValue(const QString& text, GfQuatf& result, QString& error)
{
    GfVec4f values;
    if (!parseVector<GfVec4f, 4>(text, values, error))
        return false;
    result = GfQuatf(values[0], GfVec3f(values[1], values[2], values[3]));
    return true;
}

bool
PropertyTreePrivate::parseValue(const QString& text, GfQuatd& result, QString& error)
{
    GfVec4d values;
    if (!parseVector<GfVec4d, 4>(text, values, error))
        return false;
    result = GfQuatd(values[0], GfVec3d(values[1], values[2], values[3]));
    return true;
}

QString
PropertyTreePrivate::valueText(const GfQuatf& value)
{
    const GfVec3f i = value.GetImaginary();
    return QString("(%1, %2, %3, %4)").arg(value.GetReal()).arg(i[0]).arg(i[1]).arg(i[2]);
}

QString
PropertyTreePrivate::valueText(const GfQuatd& value)
{
    const GfVec3d i = value.GetImaginary();
    return QString("(%1, %2, %3, %4)").arg(value.GetReal()).arg(i[0]).arg(i[1]).arg(i[2]);
}

template<typename T, int N>
bool
PropertyTreePrivate::parseMatrix(const QString& text, T& result, QString& error)
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

bool
PropertyTreePrivate::parseValue(const QString& text, GfMatrix2d& result, QString& error)
{
    return parseMatrix<GfMatrix2d, 2>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfMatrix3d& result, QString& error)
{
    return parseMatrix<GfMatrix3d, 3>(text, result, error);
}
bool
PropertyTreePrivate::parseValue(const QString& text, GfMatrix4d& result, QString& error)
{
    return parseMatrix<GfMatrix4d, 4>(text, result, error);
}

template<typename T>
bool
PropertyTreePrivate::tryScalarParse(const VtValue& current, const QString& text, VtValue& result, QString& error)
{
    if (!current.IsHolding<T>())
        return false;

    T value {};
    if (!parseValue(text, value, error))
        return true;  // Type matched; result remains empty to report parse failure.

    result = VtValue(value);
    return true;
}

bool
PropertyTreePrivate::parseScalar(const VtValue& current, const QString& text, VtValue& result, QString& error)
{
    if (tryScalarParse<bool>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<int>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<unsigned int>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<int64_t>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<uint64_t>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<float>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<double>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<std::string>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<TfToken>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<SdfAssetPath>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec2i>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec3i>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec4i>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec2f>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec3f>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec4f>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec2d>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec3d>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfVec4d>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfQuatf>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfQuatd>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfMatrix2d>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfMatrix3d>(current, text, result, error))
        return !result.IsEmpty();
    if (tryScalarParse<GfMatrix4d>(current, text, result, error))
        return !result.IsEmpty();

    error = QString("Unsupported editable type: %1").arg(QString::fromStdString(current.GetTypeName()));
    return false;
}

template<typename T>
bool
PropertyTreePrivate::replaceArrayElementTyped(const VtValue& current, int index, const QString& text, VtValue& result,
                                              QString& error)
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
        return true;  // Array type matched; result remains empty to report parse failure.

    array[index] = value;
    result = VtValue(array);
    return true;
}

bool
PropertyTreePrivate::replaceArrayElement(const VtValue& current, int index, const QString& text, VtValue& result,
                                         QString& error)
{
    if (replaceArrayElementTyped<bool>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<int>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<unsigned int>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<int64_t>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<uint64_t>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<float>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<double>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<std::string>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<TfToken>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<SdfAssetPath>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec2i>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec3i>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec4i>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec2f>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec3f>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec4f>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec2d>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec3d>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfVec4d>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfQuatf>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfQuatd>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfMatrix2d>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfMatrix3d>(current, index, text, result, error))
        return !result.IsEmpty();
    if (replaceArrayElementTyped<GfMatrix4d>(current, index, text, result, error))
        return !result.IsEmpty();

    error = QString("Unsupported array type: %1").arg(QString::fromStdString(current.GetTypeName()));
    return false;
}

template<typename T>
bool
PropertyTreePrivate::arrayInfoTyped(const VtValue& value, int& size)
{
    if (!value.IsHolding<VtArray<T>>())
        return false;
    size = static_cast<int>(value.UncheckedGet<VtArray<T>>().size());
    return true;
}

bool
PropertyTreePrivate::arrayInfo(const VtValue& value, int& size)
{
    return arrayInfoTyped<bool>(value, size) || arrayInfoTyped<int>(value, size)
           || arrayInfoTyped<unsigned int>(value, size) || arrayInfoTyped<int64_t>(value, size)
           || arrayInfoTyped<uint64_t>(value, size) || arrayInfoTyped<float>(value, size)
           || arrayInfoTyped<double>(value, size) || arrayInfoTyped<std::string>(value, size)
           || arrayInfoTyped<TfToken>(value, size) || arrayInfoTyped<SdfAssetPath>(value, size)
           || arrayInfoTyped<GfVec2i>(value, size) || arrayInfoTyped<GfVec3i>(value, size)
           || arrayInfoTyped<GfVec4i>(value, size) || arrayInfoTyped<GfVec2f>(value, size)
           || arrayInfoTyped<GfVec3f>(value, size) || arrayInfoTyped<GfVec4f>(value, size)
           || arrayInfoTyped<GfVec2d>(value, size) || arrayInfoTyped<GfVec3d>(value, size)
           || arrayInfoTyped<GfVec4d>(value, size) || arrayInfoTyped<GfQuatf>(value, size)
           || arrayInfoTyped<GfQuatd>(value, size) || arrayInfoTyped<GfMatrix2d>(value, size)
           || arrayInfoTyped<GfMatrix3d>(value, size) || arrayInfoTyped<GfMatrix4d>(value, size);
}

template<typename T>
bool
PropertyTreePrivate::arrayElementTextTyped(const VtValue& value, int index, QString& text)
{
    if (!value.IsHolding<VtArray<T>>())
        return false;

    const VtArray<T>& array = value.UncheckedGet<VtArray<T>>();
    if (index < 0 || index >= static_cast<int>(array.size()))
        return false;

    text = valueText(array[index]);
    return true;
}

bool
PropertyTreePrivate::arrayElementText(const VtValue& value, int index, QString& text)
{
    return arrayElementTextTyped<bool>(value, index, text) || arrayElementTextTyped<int>(value, index, text)
           || arrayElementTextTyped<unsigned int>(value, index, text)
           || arrayElementTextTyped<int64_t>(value, index, text) || arrayElementTextTyped<uint64_t>(value, index, text)
           || arrayElementTextTyped<float>(value, index, text) || arrayElementTextTyped<double>(value, index, text)
           || arrayElementTextTyped<std::string>(value, index, text)
           || arrayElementTextTyped<TfToken>(value, index, text)
           || arrayElementTextTyped<SdfAssetPath>(value, index, text)
           || arrayElementTextTyped<GfVec2i>(value, index, text) || arrayElementTextTyped<GfVec3i>(value, index, text)
           || arrayElementTextTyped<GfVec4i>(value, index, text) || arrayElementTextTyped<GfVec2f>(value, index, text)
           || arrayElementTextTyped<GfVec3f>(value, index, text) || arrayElementTextTyped<GfVec4f>(value, index, text)
           || arrayElementTextTyped<GfVec2d>(value, index, text) || arrayElementTextTyped<GfVec3d>(value, index, text)
           || arrayElementTextTyped<GfVec4d>(value, index, text) || arrayElementTextTyped<GfQuatf>(value, index, text)
           || arrayElementTextTyped<GfQuatd>(value, index, text)
           || arrayElementTextTyped<GfMatrix2d>(value, index, text)
           || arrayElementTextTyped<GfMatrix3d>(value, index, text)
           || arrayElementTextTyped<GfMatrix4d>(value, index, text);
}

QString
PropertyTreePrivate::scalarText(const VtValue& value)
{
    if (value.IsHolding<bool>())
        return valueText(value.UncheckedGet<bool>());
    if (value.IsHolding<int>())
        return valueText(value.UncheckedGet<int>());
    if (value.IsHolding<unsigned int>())
        return valueText(value.UncheckedGet<unsigned int>());
    if (value.IsHolding<int64_t>())
        return valueText(value.UncheckedGet<int64_t>());
    if (value.IsHolding<uint64_t>())
        return valueText(value.UncheckedGet<uint64_t>());
    if (value.IsHolding<float>())
        return valueText(value.UncheckedGet<float>());
    if (value.IsHolding<double>())
        return valueText(value.UncheckedGet<double>());
    if (value.IsHolding<std::string>())
        return valueText(value.UncheckedGet<std::string>());
    if (value.IsHolding<TfToken>())
        return valueText(value.UncheckedGet<TfToken>());
    if (value.IsHolding<SdfAssetPath>())
        return valueText(value.UncheckedGet<SdfAssetPath>());
    if (value.IsHolding<GfVec2i>())
        return valueText(value.UncheckedGet<GfVec2i>());
    if (value.IsHolding<GfVec3i>())
        return valueText(value.UncheckedGet<GfVec3i>());
    if (value.IsHolding<GfVec4i>())
        return valueText(value.UncheckedGet<GfVec4i>());
    if (value.IsHolding<GfVec2f>())
        return valueText(value.UncheckedGet<GfVec2f>());
    if (value.IsHolding<GfVec3f>())
        return valueText(value.UncheckedGet<GfVec3f>());
    if (value.IsHolding<GfVec4f>())
        return valueText(value.UncheckedGet<GfVec4f>());
    if (value.IsHolding<GfVec2d>())
        return valueText(value.UncheckedGet<GfVec2d>());
    if (value.IsHolding<GfVec3d>())
        return valueText(value.UncheckedGet<GfVec3d>());
    if (value.IsHolding<GfVec4d>())
        return valueText(value.UncheckedGet<GfVec4d>());
    if (value.IsHolding<GfQuatf>())
        return valueText(value.UncheckedGet<GfQuatf>());
    if (value.IsHolding<GfQuatd>())
        return valueText(value.UncheckedGet<GfQuatd>());
    if (value.IsHolding<GfMatrix2d>())
        return valueText(value.UncheckedGet<GfMatrix2d>());
    if (value.IsHolding<GfMatrix3d>())
        return valueText(value.UncheckedGet<GfMatrix3d>());
    if (value.IsHolding<GfMatrix4d>())
        return valueText(value.UncheckedGet<GfMatrix4d>());

    return QString::fromStdString(value.GetTypeName());
}

bool
PropertyTreePrivate::scalarEditable(const VtValue& value)
{
    return value.IsHolding<bool>() || value.IsHolding<int>() || value.IsHolding<unsigned int>()
           || value.IsHolding<int64_t>() || value.IsHolding<uint64_t>() || value.IsHolding<float>()
           || value.IsHolding<double>() || value.IsHolding<std::string>() || value.IsHolding<TfToken>()
           || value.IsHolding<SdfAssetPath>() || value.IsHolding<GfVec2i>() || value.IsHolding<GfVec3i>()
           || value.IsHolding<GfVec4i>() || value.IsHolding<GfVec2f>() || value.IsHolding<GfVec3f>()
           || value.IsHolding<GfVec4f>() || value.IsHolding<GfVec2d>() || value.IsHolding<GfVec3d>()
           || value.IsHolding<GfVec4d>() || value.IsHolding<GfQuatf>() || value.IsHolding<GfQuatd>()
           || value.IsHolding<GfMatrix2d>() || value.IsHolding<GfMatrix3d>() || value.IsHolding<GfMatrix4d>();
}

QString
PropertyTreePrivate::itemKey(const PropertyItem* item) const
{
    if (!item)
        return {};

    switch (item->kind()) {
    case PropertyItem::Attribute: return QString("attribute:%1").arg(qt::SdfPathToQString(item->propertyPath()));

    case PropertyItem::ArrayChunk:
        return QString("chunk:%1:%2").arg(qt::SdfPathToQString(item->propertyPath())).arg(item->chunkStart());

    case PropertyItem::ArrayElement:
        return QString("element:%1:%2").arg(qt::SdfPathToQString(item->propertyPath())).arg(item->arrayIndex());

    case PropertyItem::Group:
    default: {
        const QString own = QString("group:%1").arg(item->text(PropertyItem::Name));
        if (auto* parent = dynamic_cast<PropertyItem*>(item->parent()))
            return QString("%1/%2").arg(itemKey(parent), own);
        return QString("root:%1").arg(own);
    }
    }
}

PropertyTreePrivate::TreeState
PropertyTreePrivate::captureTreeState() const
{
    TreeState state;

    if (!d.tree)
        return state;

    if (QScrollBar* scrollBar = d.tree->verticalScrollBar())
        state.scrollValue = scrollBar->value();

    if (auto* current = dynamic_cast<PropertyItem*>(d.tree->currentItem()))
        state.current = itemKey(current);

    std::function<void(QTreeWidgetItem*)> capture = [&](QTreeWidgetItem* parent) {
        if (!parent)
            return;

        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem* child = parent->child(i);

            if (auto* item = dynamic_cast<PropertyItem*>(child)) {
                if (item->isExpanded())
                    state.expanded.insert(itemKey(item));
            }

            capture(child);
        }
    };

    capture(d.tree->invisibleRootItem());
    return state;
}

void
PropertyTreePrivate::restoreTreeState(const TreeState& state)
{
    if (!d.tree)
        return;

    PropertyItem* currentItem = nullptr;

    std::function<void(QTreeWidgetItem*)> restore = [&](QTreeWidgetItem* parent) {
        if (!parent)
            return;

        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem* child = parent->child(i);
            auto* item = dynamic_cast<PropertyItem*>(child);

            if (!item) {
                restore(child);
                continue;
            }

            const QString key = itemKey(item);

            if (!state.current.isEmpty() && key == state.current)
                currentItem = item;

            const bool expanded = state.expanded.contains(key);
            if (expanded && item->kind() == PropertyItem::ArrayChunk)
                populateChunk(item);

            item->setExpanded(expanded);
            restore(item);
        }
    };

    restore(d.tree->invisibleRootItem());

    if (currentItem)
        d.tree->setCurrentItem(currentItem);

    if (QScrollBar* scrollBar = d.tree->verticalScrollBar())
        scrollBar->setValue(state.scrollValue);
}


QString
PropertyTreePrivate::metadataText(const VtValue& value)
{
    if (value.IsEmpty())
        return {};

    std::ostringstream stream;
    stream << value;
    return QString::fromStdString(stream.str()).trimmed();
}

QString
PropertyTreePrivate::attributeBaseName(const UsdAttribute& attr)
{
    QString name = StringToQString(attr.GetName().GetString());
    qsizetype colon = name.lastIndexOf(':');
    if (colon >= 0)
        name = name.mid(colon + 1);
    return name;
}

QStringList
PropertyTreePrivate::tokenOptions(const UsdAttribute& attr)
{
    const QString name = attributeBaseName(attr);

    if (name == "visibility")
        return { "inherited", "invisible" };

    if (name == "orientation")
        return { "rightHanded", "leftHanded" };

    if (name == "purpose")
        return { "default", "render", "proxy", "guide" };

    if (name == "projection")
        return { "perspective", "orthographic" };

    if (name == "subdivisionScheme")
        return { "catmullClark", "loop", "bilinear", "none" };

    if (name == "interpolateBoundary")
        return { "none", "edgeAndCorner", "edgeOnly" };

    if (name == "faceVaryingLinearInterpolation")
        return { "all", "none", "cornersOnly", "cornersPlus1", "cornersPlus2", "boundaries", "edgeAndCorner" };

    if (name == "familyType")
        return { "nonOverlapping", "unrestricted", "partition" };

    if (name == "specifier")
        return { "def", "over", "class" };

    if (name == "kind")
        return { "model", "group", "assembly", "component", "subcomponent" };

    return {};
}

PropertyItem::Editor
PropertyTreePrivate::editorForValue(const UsdAttribute& attr, const VtValue& value)
{
    if (value.IsHolding<bool>())
        return PropertyItem::BoolEditor;

    if (value.IsHolding<TfToken>()) {
        if (!tokenOptions(attr).isEmpty())
            return PropertyItem::TokenEditor;
        return PropertyItem::TextEditor;
    }

    if (value.IsHolding<int>() || value.IsHolding<unsigned int>())
        return PropertyItem::IntegerEditor;

    if (value.IsHolding<float>() || value.IsHolding<double>())
        return PropertyItem::FloatingEditor;

    if (value.IsHolding<std::string>() || value.IsHolding<SdfAssetPath>())
        return PropertyItem::TextEditor;

    if (scalarEditable(value))
        return PropertyItem::TextEditor;

    return PropertyItem::NoEditor;
}

void
PropertyTreePrivate::configureEditor(PropertyItem* item, const UsdAttribute& attr, const VtValue& value)
{
    if (!item)
        return;

    const PropertyItem::Editor editor = editorForValue(attr, value);
    item->setEditor(editor);

    if (editor == PropertyItem::TokenEditor)
        item->setEditorOptions(tokenOptions(attr));

    if (editor == PropertyItem::IntegerEditor)
        item->setNumericRange(double(INT_MIN), double(INT_MAX));

    if (editor == PropertyItem::FloatingEditor) {
        item->setNumericRange(-1.0e12, 1.0e12);
        item->setEditorDecimals(2);

        const QString name = attributeBaseName(attr);
        if (name == "opacity" || name == "metallic" || name == "roughness")
            item->setNumericRange(0.0, 1.0);
    }
}

bool
PropertyTreePrivate::hasUnderlyingPrimOpinion(const UsdPrim& prim, const SdfLayerHandle& rootLayer)
{
    if (!prim || !prim.IsValid() || !rootLayer)
        return false;

    for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
        if (!primSpec)
            continue;

        const SdfLayerHandle layer = primSpec->GetLayer();
        if (layer && layer != rootLayer)
            return true;
    }

    return false;
}

bool
PropertyTreePrivate::isOverrideItem(const PropertyItem* item) const
{
    if (!item || item->kind() != PropertyItem::Attribute)
        return false;

    return item->data(PropertyItem::Name, OverrideRole).toBool();
}

QRect
PropertyTreePrivate::overrideIconRect(const PropertyItem* item) const
{
    if (!d.tree || !item || !isOverrideItem(item))
        return {};

    const QRect itemRect = d.tree->visualItemRect(item);
    if (!itemRect.isValid())
        return {};

    int depth = 0;
    for (QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
        ++depth;

    const int iconSize = d.tree->iconSize().width() > 0
                             ? d.tree->iconSize().width()
                             : d.tree->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, d.tree.data());

    const int columnLeft = d.tree->header()->sectionViewportPosition(PropertyItem::Name);

    const int left = columnLeft + d.tree->indentation() * (depth + 1);

    const int top = itemRect.top() + qMax(0, (itemRect.height() - iconSize) / 2);

    return QRect(left, top, iconSize, iconSize);
}

void
PropertyTreePrivate::setReadOnlyValueStyle(PropertyItem* item, bool readOnly)
{
    if (!item)
        return;

    QFont font = item->font(PropertyItem::Value);
    font.setItalic(readOnly);
    item->setFont(PropertyItem::Value, font);
}

PropertyItem*
PropertyTreePrivate::addSection(const QString& name, const QString& value)
{
    PropertyItem* item = new PropertyItem(d.tree.data());
    item->setKind(PropertyItem::Group);
    item->setText(PropertyItem::Name, name);
    item->setText(PropertyItem::Value, value);
    setReadOnlyValueStyle(item);
    item->setExpanded(true);
    return item;
}

PropertyItem*
PropertyTreePrivate::addInfo(PropertyItem* parent, const QString& name, const QString& value, const QString& toolTip)
{
    if (!parent)
        return nullptr;

    PropertyItem* item = new PropertyItem(parent);
    item->setKind(PropertyItem::Group);
    item->setText(PropertyItem::Name, name);
    item->setText(PropertyItem::Value, value);
    setReadOnlyValueStyle(item);

    if (!toolTip.isEmpty()) {
        item->setToolTip(PropertyItem::Name, toolTip);
        item->setToolTip(PropertyItem::Value, toolTip);
    }

    return item;
}

QString
PropertyTreePrivate::payloadAncestorPath(const UsdPrim& prim) const
{
    for (UsdPrim ancestor = prim.GetParent(); ancestor && !ancestor.IsPseudoRoot(); ancestor = ancestor.GetParent()) {
        if (ancestor.HasPayload())
            return qt::SdfPathToQString(ancestor.GetPath());
    }

    return {};
}

void
PropertyTreePrivate::addPrimSection(const UsdPrim& prim)
{
    PropertyItem* section = addSection("Prim");

    const QString name = StringToQString(prim.GetName().GetString());
    const QString type = prim.GetTypeName().IsEmpty() ? QStringLiteral("<untyped>")
                                                      : StringToQString(prim.GetTypeName().GetString());

    addInfo(section, "Name", name);
    addInfo(section, "Type", type);
    addInfo(section, "Path", qt::SdfPathToQString(prim.GetPath()));
    addInfo(section, "Active", prim.IsActive() ? "true" : "false");
    addInfo(section, "Defined", prim.IsDefined() ? "true" : "false");
    addInfo(section, "Loaded", prim.IsLoaded() ? "true" : "false");
    addInfo(section, "Instanceable", prim.IsInstanceable() ? "true" : "false");
    addInfo(section, "Instance", prim.IsInstance() ? "true" : "false");

    TfToken kind;
    if (UsdModelAPI(prim).GetKind(&kind) && !kind.IsEmpty())
        addInfo(section, "Kind", StringToQString(kind.GetString()));
}

void
PropertyTreePrivate::addCompositionSection(const UsdPrim& prim)
{
    PropertyItem* section = addSection("Composition");
    bool hasComposition = false;

    const SdfLayerHandle rootLayer = d.stage ? d.stage->GetRootLayer() : SdfLayerHandle();
    const auto rootSpec = rootLayer ? rootLayer->GetPrimAtPath(prim.GetPath()) : SdfPrimSpecHandle();
    const auto primStack = prim.GetPrimStack();
    const SdfLayerHandle strongestLayer = (!primStack.empty() && primStack.front()) ? primStack.front()->GetLayer()
                                                                                    : SdfLayerHandle();

    QString editSource = QStringLiteral("Composed");
    QString sourceToolTip;

    const QString payloadAncestor = payloadAncestorPath(prim);
    if (strongestLayer && rootLayer && strongestLayer == rootLayer) {
        editSource = QStringLiteral("Root layer");
    }
    else if (!payloadAncestor.isEmpty()) {
        editSource = QStringLiteral("Payload");
    }
    else if (prim.HasAuthoredReferences()) {
        editSource = QStringLiteral("Referenced layer");
    }

    if (strongestLayer) {
        const QString realPath = qt::StringToQString(strongestLayer->GetRealPath());
        const QString identifier = qt::StringToQString(strongestLayer->GetIdentifier());
        sourceToolTip = !realPath.isEmpty() ? realPath : identifier;
    }

    addInfo(section, "Edit Source", editSource, sourceToolTip);

    const bool composedOutsideRoot = strongestLayer && rootLayer && strongestLayer != rootLayer;
    addInfo(section, "Overrides", composedOutsideRoot ? QStringLiteral("Root layer") : QStringLiteral("Direct"),
            composedOutsideRoot ? QStringLiteral(
                "Property, visibility, and transform edits are authored as stronger opinions in the opened root layer.")
                                : QStringLiteral("Edits are authored directly in the opened root layer."));

    if (rootSpec && composedOutsideRoot) {
        addInfo(section, "Root Override", "Yes",
                QStringLiteral("This prim already has an authored spec in the opened root layer."));
    }

    if (prim.HasPayload()) {
        PropertyItem* payload = addInfo(section, "Payload", "Yes");
        hasComposition = true;

        VtValue metadata;
        if (prim.GetMetadata(SdfFieldKeys->Payload, &metadata) && !metadata.IsEmpty()) {
            const QString text = metadataText(metadata);
            if (!text.isEmpty())
                addInfo(payload, "Metadata", text, text);
        }
    }

    if (!payloadAncestor.isEmpty()) {
        addInfo(section, "Composed via Payload", payloadAncestor);
        hasComposition = true;
    }

    VtValue references;
    if (prim.GetMetadata(SdfFieldKeys->References, &references) && !references.IsEmpty()) {
        const QString text = metadataText(references);
        addInfo(section, "References", text.isEmpty() ? QStringLiteral("Yes") : text, text);
        hasComposition = true;
    }

    UsdVariantSets variantSets = prim.GetVariantSets();
    const std::vector<std::string> names = variantSets.GetNames();
    if (!names.empty()) {
        PropertyItem* variants = addInfo(section, "Variant Sets", QString::number(names.size()));
        variants->setExpanded(true);

        for (const std::string& name : names) {
            UsdVariantSet variantSet = prim.GetVariantSet(name);
            const QString selection = StringToQString(variantSet.GetVariantSelection());
            const std::vector<std::string> values = variantSet.GetVariantNames();

            QStringList available;
            available.reserve(static_cast<int>(values.size()));
            for (const std::string& value : values)
                available.append(StringToQString(value));

            const QString toolTip = available.isEmpty() ? QString()
                                                        : QString("Available: %1").arg(available.join(", "));

            addInfo(variants, StringToQString(name), selection.isEmpty() ? QStringLiteral("<none>") : selection,
                    toolTip);
        }

        hasComposition = true;
    }

    if (!hasComposition && !composedOutsideRoot)
        addInfo(section, "Status", "No payloads, references, or variants");
}

void
PropertyTreePrivate::addAttributesSection(const UsdPrim& prim)
{
    const std::vector<UsdAttribute> attributes = prim.GetAttributes();
    PropertyItem* section = addSection("Attributes", QString::number(attributes.size()));

    for (const UsdAttribute& attr : attributes)
        addAttribute(section, attr);
}

void
PropertyTreePrivate::addRelationshipsSection(const UsdPrim& prim)
{
    const std::vector<UsdRelationship> relationships = prim.GetRelationships();
    PropertyItem* section = addSection("Relationships", QString::number(relationships.size()));

    for (const UsdRelationship& relationship : relationships) {
        SdfPathVector targets;
        relationship.GetTargets(&targets);

        PropertyItem* item = addInfo(section, StringToQString(relationship.GetName().GetString()),
                                     targets.empty()       ? QStringLiteral("<no targets>")
                                     : targets.size() == 1 ? qt::SdfPathToQString(targets.front())
                                                           : QString("%1 targets").arg(targets.size()));

        if (targets.size() > 1) {
            for (size_t index = 0; index < targets.size(); ++index)
                addInfo(item, QString("[%1]").arg(index), qt::SdfPathToQString(targets[index]));
        }
    }
}

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
    d.update = true;
    d.stage = nullptr;
    d.path = SdfPath();
    d.tree->clear();
    d.update = false;
}

void
PropertyTreePrivate::updateStage(UsdStageRefPtr stage)
{
    QSignalBlocker blocker(d.tree.data());
    d.update = true;

    d.tree->clear();
    d.stage = stage;
    d.path = SdfPath();

    if (!stage) {
        d.update = false;
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

    d.update = false;
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
PropertyTreePrivate::addArrayElements(PropertyItem* parent, const SdfPath& propertyPath, const VtValue& value,
                                      int start, int count)
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
        child->setEditor(PropertyItem::TextEditor);
        setReadOnlyValueStyle(child, false);
    }
}

void
PropertyTreePrivate::addAttribute(PropertyItem* parent, const UsdAttribute& attr)
{
    PropertyItem* item = new PropertyItem(parent);
    item->setKind(PropertyItem::Attribute);
    item->setPropertyPath(attr.GetPath());
    item->setText(PropertyItem::Name, StringToQString(attr.GetName().GetString()));

    const SdfLayerHandle rootLayer = d.stage ? d.stage->GetRootLayer() : SdfLayerHandle();
    const UsdPrim prim = attr.GetPrim();
    const bool rootOverride = rootLayer && bool(rootLayer->GetPropertyAtPath(attr.GetPath()))
                              && hasUnderlyingPrimOpinion(prim, rootLayer);

    QStringList toolTips;
    toolTips.append(QString("Type: %1").arg(QString::fromStdString(attr.GetTypeName().GetAsToken().GetString())));

    const auto propertyStack = attr.GetPropertyStack();
    if (!propertyStack.empty() && propertyStack.front() && propertyStack.front()->GetLayer()) {
        const SdfLayerHandle layer = propertyStack.front()->GetLayer();
        const QString realPath = qt::StringToQString(layer->GetRealPath());
        const QString identifier = qt::StringToQString(layer->GetIdentifier());
        toolTips.append(QString("Strongest opinion: %1").arg(!realPath.isEmpty() ? realPath : identifier));
    }

    item->setData(PropertyItem::Name, OverrideRole, rootOverride);

    if (rootOverride) {
        toolTips.append(QStringLiteral("Root-layer override"));
        toolTips.append(QStringLiteral("Click the override icon to reset this property"));
        item->setIcon(PropertyItem::Name, QIcon(style()->icon(Style::Override, Style::UIScale::Small)));
        QFont nameFont = item->font(PropertyItem::Name);
        nameFont.setBold(true);
        item->setFont(PropertyItem::Name, nameFont);
    }

    item->setToolTip(PropertyItem::Name, toolTips.join('\n'));

    VtValue value;
    if (!attr.Get(&value)) {
        item->setText(PropertyItem::Value, "<no default>");
        QString valueToolTip = QStringLiteral("No composed default value");
        if (rootOverride)
            valueToolTip += QStringLiteral("\nRoot-layer override");
        item->setToolTip(PropertyItem::Value, valueToolTip);
        setReadOnlyValueStyle(item);
        return;
    }

    int arraySize = 0;
    if (arrayInfo(value, arraySize)) {
        item->setText(PropertyItem::Value, QString("%1 values").arg(arraySize));
        QString valueToolTip = QString::fromStdString(value.GetTypeName());
        if (rootOverride)
            valueToolTip += QStringLiteral("\nRoot-layer override");
        item->setToolTip(PropertyItem::Value, valueToolTip);
        setReadOnlyValueStyle(item);

        if (arraySize <= d.chunkSize) {
            addArrayElements(item, attr.GetPath(), value, 0, arraySize);
        }
        else {
            for (int start = 0; start < arraySize; start += d.chunkSize) {
                const int count = std::min(d.chunkSize, arraySize - start);
                PropertyItem* chunk = new PropertyItem(item);
                chunk->setKind(PropertyItem::ArrayChunk);
                chunk->setPropertyPath(attr.GetPath());
                chunk->setChunkRange(start, count);
                chunk->setText(PropertyItem::Name, QString("[%1..%2]").arg(start).arg(start + count - 1));
                chunk->setText(PropertyItem::Value, QString("%1 values").arg(count));
                setReadOnlyValueStyle(chunk);

                // Dummy child gives the chunk an expansion arrow. It is replaced lazily.
                PropertyItem* dummy = new PropertyItem(chunk);
                dummy->setKind(PropertyItem::Group);
                dummy->setText(PropertyItem::Name, "...");
            }
        }
        return;
    }

    item->setText(PropertyItem::Value, scalarText(value));
    QString valueToolTip = QString::fromStdString(value.GetTypeName());
    if (rootOverride)
        valueToolTip += QStringLiteral("\nRoot-layer override");
    item->setToolTip(PropertyItem::Value, valueToolTip);

    const bool editable = scalarEditable(value);
    item->setValueEditable(editable);
    configureEditor(item, attr, value);
    setReadOnlyValueStyle(item, !editable);
}

void
PropertyTreePrivate::updateSelection(const QList<SdfPath>& paths)
{
    const bool preserveState = paths.size() == 1 && !d.path.IsEmpty() && paths.first() == d.path;

    const TreeState treeState = preserveState ? captureTreeState() : TreeState();

    QSignalBlocker blocker(d.tree.data());
    d.update = true;
    d.tree->clear();

    if (!paths.isEmpty()) {
        if (paths.size() > 1) {
            PropertyItem* multiItem = new PropertyItem(d.tree.data());
            multiItem->setKind(PropertyItem::Group);
            multiItem->setText(PropertyItem::Name, "[Multiple selection]");
            multiItem->setExpanded(true);
            d.path = SdfPath();
            d.update = false;
            return;
        }

        const SdfPath path = paths.first();
        if (!d.stage) {
            d.update = false;
            return;
        }

        UsdPrim prim;
        {
            READ_LOCKER(locker, d.context ? d.context->stageLock() : session()->stageLock(), "stageLock");
            prim = d.stage->GetPrimAtPath(path);
        }

        if (!prim) {
            d.update = false;
            return;
        }

        {
            READ_LOCKER(locker, d.context ? d.context->stageLock() : session()->stageLock(), "stageLock");
            addPrimSection(prim);
            addCompositionSection(prim);
            addAttributesSection(prim);
            addRelationshipsSection(prim);
        }

        d.path = path;

        if (preserveState)
            restoreTreeState(treeState);

        d.update = false;
        return;
    }

    d.path = SdfPath();
    d.update = false;

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
    const bool previousUpdate = d.update;
    d.update = true;

    while (item->childCount() > 0)
        delete item->takeChild(0);

    addArrayElements(item, item->propertyPath(), value, item->chunkStart(), item->chunkCount());
    item->setChunkPopulated(true);

    d.update = previousUpdate;
}

void
PropertyTreePrivate::itemExpanded(QTreeWidgetItem* baseItem)
{
    if (d.update)
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
    if (d.update || column != PropertyItem::Value)
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
        MessageDialog::warning(d.tree.data(), tr("Invalid property value"),
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

    setItemDelegate(new PropertyDelegate(this));

    setColumnSelectable(PropertyItem::Name, true);
    setColumnSelectable(PropertyItem::Value, true);

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
PropertyTree::mousePressEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton) {
        auto* item = dynamic_cast<PropertyItem*>(itemAt(event->pos()));

        if (item && p->isOverrideItem(item)) {
            const QRect iconRect = p->overrideIconRect(item);

            if (iconRect.contains(event->pos())) {
                if (ViewContext* viewContext = context())
                    viewContext->run(new Command(resetAttributeOverride(item->propertyPath())));
                else
                    session()->commandStack()->run(new Command(resetAttributeOverride(item->propertyPath())));

                event->accept();
                return;
            }
        }
    }

    TreeWidget::mousePressEvent(event);
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
