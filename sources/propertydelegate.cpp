// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertydelegate.h"
#include "propertyitem.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>

namespace stageviz {

PropertyDelegate::PropertyDelegate(QObject* parent)
    : TreeWidget::ItemDelegate(parent)
{}

PropertyDelegate::~PropertyDelegate() = default;

QWidget*
PropertyDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid() || index.column() != PropertyItem::Value)
        return nullptr;

    const PropertyItem::Editor editorType = PropertyItem::Editor(index.data(PropertyItem::EditorRole).toInt());

    switch (editorType) {
    case PropertyItem::BoolEditor: {
        auto* combo = new QComboBox(parent);
        combo->addItem("false");
        combo->addItem("true");
        combo->setEditable(false);
        return combo;
    }

    case PropertyItem::TokenEditor: {
        const QStringList options = index.data(PropertyItem::EditorOptionsRole).toStringList();
        if (options.isEmpty())
            break;

        auto* combo = new QComboBox(parent);
        combo->addItems(options);
        combo->setEditable(false);
        return combo;
    }

    case PropertyItem::IntegerEditor: {
        auto* spin = new QSpinBox(parent);
        const double minimum = index.data(PropertyItem::EditorMinimumRole).toDouble();
        const double maximum = index.data(PropertyItem::EditorMaximumRole).toDouble();
        spin->setRange(int(qMax(double(INT_MIN), minimum)), int(qMin(double(INT_MAX), maximum)));
        return spin;
    }

    case PropertyItem::FloatingEditor: {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setRange(index.data(PropertyItem::EditorMinimumRole).toDouble(),
                       index.data(PropertyItem::EditorMaximumRole).toDouble());
        spin->setDecimals(index.data(PropertyItem::EditorDecimalsRole).toInt());
        spin->setSingleStep(0.1);
        return spin;
    }

    case PropertyItem::TextEditor: return new QLineEdit(parent);

    case PropertyItem::NoEditor:
    default: break;
    }

    return TreeWidget::ItemDelegate::createEditor(parent, option, index);
}

void
PropertyDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    const QString text = index.data(Qt::EditRole).toString();

    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        int i = combo->findText(text);
        if (i < 0)
            i = combo->findText(text, Qt::MatchFixedString);
        if (i >= 0)
            combo->setCurrentIndex(i);
        return;
    }

    if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
        bool ok = false;
        const int value = text.toInt(&ok);
        if (ok)
            spin->setValue(value);
        return;
    }

    if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (ok)
            spin->setValue(value);
        return;
    }

    if (auto* line = qobject_cast<QLineEdit*>(editor)) {
        line->setText(text);
        line->selectAll();
        return;
    }

    TreeWidget::ItemDelegate::setEditorData(editor, index);
}

void
PropertyDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        model->setData(index, combo->currentText(), Qt::EditRole);
        return;
    }

    if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
        model->setData(index, QString::number(spin->value()), Qt::EditRole);
        return;
    }

    if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
        model->setData(index, QString::number(spin->value(), 'g', 12), Qt::EditRole);
        return;
    }

    if (auto* line = qobject_cast<QLineEdit*>(editor)) {
        model->setData(index, line->text(), Qt::EditRole);
        return;
    }

    TreeWidget::ItemDelegate::setModelData(editor, model, index);
}

}  // namespace stageviz
