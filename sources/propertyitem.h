// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "treeitem.h"
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
    enum Column {
        Name = 0,
        Value
    };

    enum Kind {
        Group,
        Attribute,
        ArrayChunk,
        ArrayElement
    };

    enum Editor {
        NoEditor = 0,
        TextEditor,
        BoolEditor,
        TokenEditor,
        IntegerEditor,
        FloatingEditor
    };

    enum Role {
        EditorRole = Qt::UserRole + 100,
        EditorOptionsRole,
        EditorMinimumRole,
        EditorMaximumRole,
        EditorDecimalsRole
    };

    PropertyItem(QTreeWidget* parent);
    PropertyItem(QTreeWidgetItem* parent);
    virtual ~PropertyItem();

    TreeItem::ItemStates itemStates() const;

    Kind kind() const;
    void setKind(Kind kind);

    SdfPath propertyPath() const;
    void setPropertyPath(const SdfPath& path);

    int arrayIndex() const;
    void setArrayIndex(int index);

    int chunkStart() const;
    int chunkCount() const;
    void setChunkRange(int start, int count);

    bool chunkPopulated() const;
    void setChunkPopulated(bool populated);

    bool valueEditable() const;
    void setValueEditable(bool editable);

    Editor editor() const;
    void setEditor(Editor editor);

    QStringList editorOptions() const;
    void setEditorOptions(const QStringList& options);

    void setNumericRange(double minimum, double maximum);
    double editorMinimum() const;
    double editorMaximum() const;

    void setEditorDecimals(int decimals);
    int editorDecimals() const;

private:
    QScopedPointer<PropertyItemPrivate> p;
};

}  // namespace stageviz
