// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "treeitem.h"
#include <QList>
#include <QStringList>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PropertyItemPrivate;

/**
 * @class PropertyItem
 * @brief Tree item representing a USD property or an editable array element.
 *
 * The item stores lightweight presentation/editor metadata only. USD reads and
 * writes remain owned by PropertyTree and the command system.
 */
class PropertyItem : public TreeItem {
public:
    /**
     * @brief Columns used for the property name and displayed value.
     */
    enum Column { Name = 0, Value };

    /**
     * @brief Distinguishes groups, attributes, array pages, and individual elements.
     */
    enum Kind { Group, Attribute, ArrayChunk, ArrayElement };

    /**
     * @brief Selects the editor used by the property delegate.
     */
    enum Editor { NoEditor = 0, TextEditor, BoolEditor, TokenEditor, IntegerEditor, FloatingEditor };

    /**
     * @brief Qt data roles carrying editor configuration and value navigation metadata.
     */
    enum Role {
        EditorRole = Qt::UserRole + 100,
        EditorOptionsRole,
        EditorMinimumRole,
        EditorMaximumRole,
        EditorDecimalsRole,
        MixedValueRole,
        ValuePathRole
    };

    /**
     * @brief Creates an item owned by the supplied tree.
     */
    PropertyItem(QTreeWidget* parent);
    /**
     * @brief Creates an item owned by the supplied parent item.
     */
    PropertyItem(QTreeWidgetItem* parent);
    /**
     * @brief Releases the item's editor metadata.
     */
    virtual ~PropertyItem();

    /**
     * @brief Returns the presentation state used by the shared tree delegate.
     */
    TreeItem::ItemStates itemStates() const;

    /**
     * @brief Returns the row's semantic kind.
     */
    Kind kind() const;
    /**
     * @brief Sets the row kind without authoring USD.
     */
    void setKind(Kind kind);

    /**
     * @brief Returns the primary represented property path.
     */
    SdfPath propertyPath() const;
    /**
     * @brief Sets the primary property represented by this row.
     */
    void setPropertyPath(const SdfPath& path);

    /**
     * @brief Returns all property paths represented by a multi-selection row.
     */
    QList<SdfPath> propertyPaths() const;
    /**
     * @brief Stores the properties edited together by this row.
     */
    void setPropertyPaths(const QList<SdfPath>& paths);

    /**
     * @brief Returns a USD path referenced by the displayed value, for navigation.
     */
    SdfPath valuePath() const;
    /**
     * @brief Stores the value's navigation target.
     */
    void setValuePath(const SdfPath& path);

    /**
     * @brief Reports whether the represented properties have different values.
     */
    bool mixedValue() const;
    /**
     * @brief Sets the mixed-value indicator.
     */
    void setMixedValue(bool mixed);

    /**
     * @brief Returns the represented array element index.
     */
    int arrayIndex() const;
    /**
     * @brief Stores the represented array element index.
     */
    void setArrayIndex(int index);

    /**
     * @brief Returns the first array index in this chunk.
     */
    int chunkStart() const;
    /**
     * @brief Returns the number of array elements in this chunk.
     */
    int chunkCount() const;
    /**
     * @brief Stores the chunk's starting index and element count.
     */
    void setChunkRange(int start, int count);

    /**
     * @brief Reports whether child rows have been created for this chunk.
     */
    bool chunkPopulated() const;
    /**
     * @brief Records whether this chunk's child rows have been populated.
     */
    void setChunkPopulated(bool populated);

    /**
     * @brief Returns whether value editing is enabled for this row.
     */
    bool valueEditable() const;
    /**
     * @brief Enables or disables editing of the displayed value.
     */
    void setValueEditable(bool editable);

    /**
     * @brief Returns the configured value editor type.
     */
    Editor editor() const;
    /**
     * @brief Selects the value editor type.
     */
    void setEditor(Editor editor);

    /**
     * @brief Returns the choices offered by the token editor.
     */
    QStringList editorOptions() const;
    /**
     * @brief Sets the choices offered by the token editor.
     */
    void setEditorOptions(const QStringList& options);

    /**
     * @brief Sets the numeric editor's minimum and maximum values.
     */
    void setNumericRange(double minimum, double maximum);
    /**
     * @brief Returns the numeric editor's lower bound.
     */
    double editorMinimum() const;
    /**
     * @brief Returns the numeric editor's upper bound.
     */
    double editorMaximum() const;

    /**
     * @brief Sets the number of decimal places displayed by the numeric editor.
     */
    void setEditorDecimals(int decimals);
    /**
     * @brief Returns the configured decimal precision.
     */
    int editorDecimals() const;

private:
    QScopedPointer<PropertyItemPrivate> p;
};

}  // namespace stageviz
