// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialmenu.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialMenuPrivate {
public:
    static QString shaderId(const UsdPrim& prim);
    static bool isMaterialXShader(const UsdPrim& prim);
    static QList<MaterialMenu::Choice> materialXChoices(const SdfValueTypeName& targetType);
    static QList<MaterialMenu::Choice> usdChoices(const SdfValueTypeName& targetType);
    static void addMaterialXMenu(QMenu* root, const QList<MaterialMenu::Choice>& choices, QObject* receiver,
                                 const std::function<void(const MaterialMenu::Choice&)>& callback);
    static void addUsdMenu(QMenu* root, const QList<MaterialMenu::Choice>& choices, QObject* receiver,
                           const std::function<void(const MaterialMenu::Choice&)>& callback);
};

QString
MaterialMenuPrivate::shaderId(const UsdPrim& prim)
{
    if (!prim)
        return {};

    VtValue value;
    const UsdAttribute id = prim.GetAttribute(TfToken("info:id"));
    if (!id || !id.Get(&value))
        return {};

    if (value.IsHolding<TfToken>())
        return QString::fromStdString(value.UncheckedGet<TfToken>().GetString());
    if (value.IsHolding<std::string>())
        return QString::fromStdString(value.UncheckedGet<std::string>());
    return {};
}

bool
MaterialMenuPrivate::isMaterialXShader(const UsdPrim& prim)
{
    return shaderId(prim).startsWith(QStringLiteral("ND_"));
}

QList<MaterialMenu::Choice>
MaterialMenuPrivate::materialXChoices(const SdfValueTypeName& targetType)
{
    const QList<MaterialXNodeDefinition> defs = targetType.GetAsToken().IsEmpty()
                                                    ? MaterialUtils::materialXNodeDefinitions()
                                                    : MaterialUtils::compatibleMaterialXNodes(targetType);

    QList<MaterialMenu::Choice> result;
    QSet<QString> seen;

    for (const MaterialXNodeDefinition& def : defs) {
        if (def.nodeDef.isEmpty() || seen.contains(def.nodeDef))
            continue;

        seen.insert(def.nodeDef);

        MaterialMenu::Choice choice;
        choice.family = MaterialMenu::Family::MaterialX;
        choice.group = def.group.isEmpty() ? QStringLiteral("Other") : def.group;
        choice.nodeName = def.node;
        choice.displayName = def.node;
        choice.dataType = def.outputType;
        choice.nodeDef = def.nodeDef;
        choice.materialXDefinition = def;
        result.append(choice);
    }

    return result;
}

QList<MaterialMenu::Choice>
MaterialMenuPrivate::usdChoices(const SdfValueTypeName& targetType)
{
    const QList<ShaderNodeDefinition> defs = targetType.GetAsToken().IsEmpty()
                                                 ? MaterialUtils::usdShaderNodes()
                                                 : MaterialUtils::compatibleUsdShaderNodes(targetType);

    QList<MaterialMenu::Choice> result;
    QSet<QString> seen;

    for (const ShaderNodeDefinition& def : defs) {
        const QString key = QStringLiteral("%1\n%2").arg(def.shaderId,
                                                         QString::fromStdString(def.outputName.GetString()));
        if (seen.contains(key))
            continue;

        seen.insert(key);

        MaterialMenu::Choice choice;
        choice.family = MaterialMenu::Family::UsdPreview;
        choice.group = def.group.isEmpty() ? QStringLiteral("Other") : def.group;
        choice.nodeName = def.node;
        choice.displayName = def.node;
        choice.shaderId = def.shaderId;
        choice.outputName = def.outputName;
        choice.outputType = def.outputType;
        choice.dataType = QString::fromStdString(def.outputType.GetAsToken().GetString());
        result.append(choice);
    }

    return result;
}

void
MaterialMenuPrivate::addMaterialXMenu(QMenu* root, const QList<MaterialMenu::Choice>& choices, QObject* receiver,
                                      const std::function<void(const MaterialMenu::Choice&)>& callback)
{
    if (!root || choices.isEmpty())
        return;

    QHash<QString, QHash<QString, QList<MaterialMenu::Choice>>> grouped;
    for (const MaterialMenu::Choice& choice : choices)
        grouped[choice.group][choice.nodeName].append(choice);

    QStringList groups = grouped.keys();
    std::sort(groups.begin(), groups.end(),
              [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });

    for (const QString& groupName : groups) {
        QMenu* group = root->addMenu(groupName);
        QStringList nodeNames = grouped[groupName].keys();
        std::sort(nodeNames.begin(), nodeNames.end(),
                  [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });

        for (const QString& nodeName : nodeNames) {
            QList<MaterialMenu::Choice> variants = grouped[groupName][nodeName];
            std::sort(variants.begin(), variants.end(),
                      [](const MaterialMenu::Choice& a, const MaterialMenu::Choice& b) {
                          if (a.dataType != b.dataType)
                              return a.dataType < b.dataType;
                          return a.nodeDef < b.nodeDef;
                      });

            if (variants.size() == 1) {
                const MaterialMenu::Choice choice = variants.first();
                QAction* action = group->addAction(nodeName);
                action->setToolTip(QStringLiteral("%1\nOutput: %2").arg(choice.nodeDef, choice.dataType));
                QObject::connect(action, &QAction::triggered, receiver, [choice, callback]() { callback(choice); });
                continue;
            }

            QMenu* typed = group->addMenu(nodeName);
            QSet<QString> usedLabels;

            for (const MaterialMenu::Choice& choice : variants) {
                QString label = choice.dataType.isEmpty() ? QStringLiteral("default") : choice.dataType;
                if (usedLabels.contains(label))
                    label = QStringLiteral("%1 — %2").arg(label, choice.nodeDef);

                usedLabels.insert(label);

                QAction* action = typed->addAction(label);
                action->setToolTip(QStringLiteral("%1\nOutput: %2").arg(choice.nodeDef, choice.dataType));
                QObject::connect(action, &QAction::triggered, receiver, [choice, callback]() { callback(choice); });
            }
        }
    }
}

void
MaterialMenuPrivate::addUsdMenu(QMenu* root, const QList<MaterialMenu::Choice>& choices, QObject* receiver,
                                const std::function<void(const MaterialMenu::Choice&)>& callback)
{
    if (!root || choices.isEmpty())
        return;

    QHash<QString, QList<MaterialMenu::Choice>> grouped;
    for (const MaterialMenu::Choice& choice : choices)
        grouped[choice.group].append(choice);

    QStringList groups = grouped.keys();
    std::sort(groups.begin(), groups.end(),
              [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });

    for (const QString& groupName : groups) {
        QMenu* group = root->addMenu(groupName);
        QList<MaterialMenu::Choice> defs = grouped[groupName];

        std::sort(defs.begin(), defs.end(), [](const MaterialMenu::Choice& a, const MaterialMenu::Choice& b) {
            return a.displayName.localeAwareCompare(b.displayName) < 0;
        });

        for (const MaterialMenu::Choice& choice : defs) {
            QAction* action = group->addAction(choice.displayName);
            action->setToolTip(QStringLiteral("%1\nOutput: %2")
                                   .arg(choice.shaderId, QString::fromStdString(choice.outputName.GetString())));
            QObject::connect(action, &QAction::triggered, receiver, [choice, callback]() { callback(choice); });
        }
    }
}


QList<MaterialMenu::Choice>
MaterialMenu::choices(const Request& request)
{
    QList<Choice> result;

    if (!request.usdPreviewOnly)
        result.append(MaterialMenuPrivate::materialXChoices(request.targetType));

    if (!request.materialXOnly)
        result.append(MaterialMenuPrivate::usdChoices(request.targetType));

    return result;
}

void
MaterialMenu::populate(QMenu* menu, const Request& request, QObject* receiver,
                       const std::function<void(const Choice&)>& callback)
{
    if (!menu || !receiver || !callback)
        return;

    // Menus and search must use the same candidate model.
    const QList<Choice> available = choices(request);
    QList<Choice> mtlx;
    QList<Choice> usd;
    for (const Choice& choice : available) {
        if (choice.family == Family::MaterialX)
            mtlx.append(choice);
        else if (choice.family == Family::UsdPreview)
            usd.append(choice);
    }

    auto addMtlx = [&]() {
        if (mtlx.isEmpty())
            return;

        QMenu* root = menu;
        if (!request.flattenFamily)
            root = menu->addMenu(QObject::tr("MaterialX Nodes"));

        if (request.includeSearch) {
            QAction* search = root->addAction(QObject::tr("Search MaterialX Nodes..."));
            QWidget* parent = qobject_cast<QWidget*>(receiver);
            if (!parent)
                parent = menu->parentWidget();

            // Defer until QMenu releases its native grab on macOS.
            const QPointer<QWidget> searchParent(parent);
            QObject::connect(search, &QAction::triggered, receiver, [searchParent, request, callback]() {
                QTimer::singleShot(0, [searchParent, request, callback]() {
                    showMaterialXSearch(searchParent.data(), request, callback);
                });
            });
            root->addSeparator();
        }

        MaterialMenuPrivate::addMaterialXMenu(root, mtlx, receiver, callback);
    };

    auto addUsd = [&]() {
        if (usd.isEmpty())
            return;

        QMenu* root = menu;
        if (!request.flattenFamily)
            root = menu->addMenu(QObject::tr("USD Preview Nodes"));

        MaterialMenuPrivate::addUsdMenu(root, usd, receiver, callback);
    };

    if (request.materialXFirst) {
        addMtlx();
        addUsd();
    }
    else {
        addUsd();
        addMtlx();
    }

    if (menu->actions().isEmpty()) {
        QAction* empty = menu->addAction(request.targetType.GetAsToken().IsEmpty()
                                             ? QObject::tr("No nodes available")
                                             : QObject::tr("No compatible nodes"));
        empty->setEnabled(false);
    }
}

void
MaterialMenu::showMaterialXSearch(QWidget* parent, const Request& request,
                                  const std::function<void(const Choice&)>& callback)
{
    QList<Choice> defs;
    for (const Choice& choice : choices(request)) {
        if (choice.family == Family::MaterialX)
            defs.append(choice);
    }

    if (defs.isEmpty() || !callback)
        return;

    // Keep this modeless: nested modal dialogs from QMenu can retain the native grab on macOS.
    QWidget* owner = parent ? parent->window() : QApplication::activeWindow();
    auto* dialog = new QDialog(owner, Qt::Tool);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setModal(false);
    dialog->setWindowTitle(request.targetType.GetAsToken().IsEmpty() ? QObject::tr("Create MaterialX Node")
                                                                     : QObject::tr("Connect MaterialX Node"));
    dialog->resize(620, 560);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(10, 10, 10, 14);
    layout->setSpacing(8);

    auto* filter = new QLineEdit(dialog);
    filter->setPlaceholderText(QObject::tr("Filter MaterialX nodes..."));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);

    auto* tree = new QTreeWidget(dialog);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setItemsExpandable(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(tree, 1);

    constexpr int choiceRole = Qt::UserRole;
    constexpr int isChoiceRole = Qt::UserRole + 1;

    QHash<QString, QList<int>> grouped;
    for (int index = 0; index < defs.size(); ++index)
        grouped[defs[index].group].append(index);

    QStringList groups = grouped.keys();
    std::sort(groups.begin(), groups.end(),
              [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });

    QList<QTreeWidgetItem*> leafItems;
    for (const QString& groupName : groups) {
        auto* groupItem = new QTreeWidgetItem(tree);
        groupItem->setText(0, groupName);
        groupItem->setData(0, isChoiceRole, false);
        groupItem->setFlags((groupItem->flags() | Qt::ItemIsEnabled) & ~Qt::ItemIsSelectable);

        QList<int> indices = grouped.value(groupName);
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            const Choice& ca = defs[a];
            const Choice& cb = defs[b];
            const int nameCompare = ca.displayName.localeAwareCompare(cb.displayName);
            if (nameCompare != 0)
                return nameCompare < 0;
            return ca.nodeDef.localeAwareCompare(cb.nodeDef) < 0;
        });

        QHash<QString, int> duplicateCount;
        for (int index : indices) {
            const Choice& choice = defs[index];
            duplicateCount[choice.displayName + QLatin1Char('|') + choice.dataType]++;
        }

        for (int index : indices) {
            const Choice& choice = defs[index];
            QString label = choice.displayName;
            if (!choice.dataType.isEmpty())
                label += QStringLiteral("  [%1]").arg(choice.dataType);
            if (duplicateCount.value(choice.displayName + QLatin1Char('|') + choice.dataType) > 1)
                label += QStringLiteral("  —  %1").arg(choice.nodeDef);

            auto* item = new QTreeWidgetItem(groupItem);
            item->setText(0, label);
            item->setData(0, choiceRole, index);
            item->setData(0, isChoiceRole, true);
            item->setFlags(item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(0, QStringLiteral("%1\nOutput: %2").arg(choice.nodeDef, choice.dataType));
            leafItems.append(item);
        }

        groupItem->setExpanded(true);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    ok->setEnabled(false);

    auto selectedChoiceIndex = [tree]() -> int {
        QTreeWidgetItem* current = tree->currentItem();
        if (!current || !current->data(0, isChoiceRole).toBool())
            return -1;
        return current->data(0, choiceRole).toInt();
    };

    auto updateOk = [ok, selectedChoiceIndex, defs]() {
        const int index = selectedChoiceIndex();
        ok->setEnabled(index >= 0 && index < defs.size());
    };

    auto applyFilter = [tree, defs, groups, grouped](const QString& text) {
        const QString needle = text.trimmed();
        for (int groupIndex = 0; groupIndex < tree->topLevelItemCount(); ++groupIndex) {
            QTreeWidgetItem* groupItem = tree->topLevelItem(groupIndex);
            if (!groupItem)
                continue;

            bool anyVisible = false;
            for (int childIndex = 0; childIndex < groupItem->childCount(); ++childIndex) {
                QTreeWidgetItem* item = groupItem->child(childIndex);
                if (!item)
                    continue;
                const int index = item->data(0, choiceRole).toInt();
                if (index < 0 || index >= defs.size())
                    continue;
                const Choice& choice = defs[index];
                const bool visible = needle.isEmpty() || choice.displayName.contains(needle, Qt::CaseInsensitive)
                                     || choice.nodeName.contains(needle, Qt::CaseInsensitive)
                                     || choice.nodeDef.contains(needle, Qt::CaseInsensitive)
                                     || choice.group.contains(needle, Qt::CaseInsensitive)
                                     || choice.dataType.contains(needle, Qt::CaseInsensitive);
                item->setHidden(!visible);
                anyVisible |= visible;
            }
            groupItem->setHidden(!anyVisible);
            if (!needle.isEmpty() && anyVisible)
                groupItem->setExpanded(true);
        }
    };

    auto selectFirstVisible = [tree, leafItems]() {
        for (QTreeWidgetItem* item : leafItems) {
            if (item && !item->isHidden() && item->parent() && !item->parent()->isHidden()) {
                tree->setCurrentItem(item);
                return;
            }
        }
        tree->setCurrentItem(nullptr);
    };

    QObject::connect(filter, &QLineEdit::textChanged, dialog,
                     [applyFilter, selectFirstVisible, updateOk](const QString& text) {
                         applyFilter(text);
                         selectFirstVisible();
                         updateOk();
                     });
    QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog,
                     [updateOk](QTreeWidgetItem*, QTreeWidgetItem*) { updateOk(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, selectedChoiceIndex, defs, callback]() {
        const int index = selectedChoiceIndex();
        if (index < 0 || index >= defs.size())
            return;
        callback(defs[index]);
        dialog->close();
    });
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, dialog,
                     [dialog, selectedChoiceIndex, defs, callback](QTreeWidgetItem*, int) {
                         const int index = selectedChoiceIndex();
                         if (index < 0 || index >= defs.size())
                             return;
                         callback(defs[index]);
                         dialog->close();
                     });

    selectFirstVisible();
    updateOk();
    filter->setFocus();

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

MaterialMenu::Compatibility
MaterialMenu::compatibility(const SdfValueTypeName& outputType, const SdfValueTypeName& inputType,
                            bool materialXToMaterialX)
{
    if (outputType.GetAsToken().IsEmpty() || inputType.GetAsToken().IsEmpty())
        return Compatibility::Incompatible;

    if (outputType == inputType)
        return Compatibility::Compatible;

    // Float2 <-> TexCoord2f is the one intentional storage-equivalent
    // connection supported here. Do not treat Float3 and Color3f as generally
    // interchangeable: Storm can crash while compiling some USD Preview
    // networks such as UsdPrimvarReader_float3.result -> diffuseColor.
    const bool vec2Texcoord = (outputType == SdfValueTypeNames->Float2 && inputType == SdfValueTypeNames->TexCoord2f)
                              || (outputType == SdfValueTypeNames->TexCoord2f
                                  && inputType == SdfValueTypeNames->Float2);
    if (vec2Texcoord)
        return Compatibility::Compatible;

    Q_UNUSED(materialXToMaterialX);
    return Compatibility::Incompatible;
}

MaterialMenu::Compatibility
MaterialMenu::connectionCompatibility(const UsdStageRefPtr& stage, const SdfPath& sourceOutputPath,
                                      const SdfPath& targetInputPath)
{
    if (!stage || sourceOutputPath.IsEmpty() || targetInputPath.IsEmpty())
        return Compatibility::Incompatible;

    const UsdAttribute output = stage->GetAttributeAtPath(sourceOutputPath);
    const UsdAttribute input = stage->GetAttributeAtPath(targetInputPath);
    if (!output || !input)
        return Compatibility::Incompatible;

    const bool mtlxToMtlx = MaterialMenuPrivate::isMaterialXShader(stage->GetPrimAtPath(sourceOutputPath.GetPrimPath()))
                            && MaterialMenuPrivate::isMaterialXShader(
                                stage->GetPrimAtPath(targetInputPath.GetPrimPath()));
    return compatibility(output.GetTypeName(), input.GetTypeName(), mtlxToMtlx);
}

}  // namespace stageviz
