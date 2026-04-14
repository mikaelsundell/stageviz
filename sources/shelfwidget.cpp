// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "shelfwidget.h"
#include "application.h"
#include "mime.h"
#include "qtutils.h"
#include "roles.h"
#include "shelflist.h"
#include "style.h"
#include <QAbstractItemModel>
#include <QApplication>
#include <QBuffer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVariantMap>

namespace stageviz {

class ShelfWidgetPrivate : public QObject {
    Q_OBJECT
public:
    ShelfWidgetPrivate();
    ~ShelfWidgetPrivate();
    void init();
    void addScript(const QString& code, const QString& name = QString(), const QByteArray& iconBytes = QByteArray());
    void editScript(QListWidgetItem* item);
    void removeScript(QListWidgetItem* item);
    int count() const;
    void clear();

public Q_SLOTS:
    void contextMenuEvent(const QPoint& pos);

public:
    QString titleFromText(const QString& text, int maxLength, const QString& fallback = QString(),
                          const QString& prefixToStrip = QString());
    QVariantList toVariantList() const;
    void fromVariantList(const QVariantList& scripts);
    QString uniqueTitle(const QString& base, const QListWidgetItem* ignoreItem = nullptr) const;
    QListWidgetItem* itemAt(const QPoint& pos) const;
    void itemIcon(QListWidgetItem* item, const QByteArray& iconBytes);
    struct Data {
        QPointer<ShelfList> list;
        QPointer<ShelfWidget> shelf;
    };
    Data d;
};

ShelfWidgetPrivate::ShelfWidgetPrivate() {}

ShelfWidgetPrivate::~ShelfWidgetPrivate() {}

void
ShelfWidgetPrivate::init()
{
    d.list = new ShelfList(d.shelf.data());

    QHBoxLayout* layout = new QHBoxLayout(d.shelf);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d.list);
    // connect
    QObject::connect(d.list, &QWidget::customContextMenuRequested, this, &ShelfWidgetPrivate::contextMenuEvent);
    QObject::connect(d.list->model(), &QAbstractItemModel::rowsInserted, this, [this]() { Q_EMIT d.shelf->changed(); });
    QObject::connect(d.list->model(), &QAbstractItemModel::rowsRemoved, this, [this]() { Q_EMIT d.shelf->changed(); });
    QObject::connect(d.list->model(), &QAbstractItemModel::modelReset, this, [this]() { Q_EMIT d.shelf->changed(); });
    QObject::connect(d.list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const QString editedName = item->text().trimmed();
        const QString newName = uniqueTitle(editedName.isEmpty() ? QStringLiteral("Script") : editedName, item);

        const bool blocked = d.list->blockSignals(true);
        item->setText(QString());
        item->setData(roles::shelf::scriptName, newName);
        item->setToolTip(newName);
        d.list->blockSignals(blocked);

        Q_EMIT d.shelf->changed();
    });
}

void
ShelfWidgetPrivate::addScript(const QString& code, const QString& name, const QByteArray& iconBytes)
{
    const QString trimmed = qt::normalizeNewlines(code).trimmed();
    if (trimmed.isEmpty())
        return;

    const QString title = uniqueTitle(
        name.isEmpty() ? titleFromText(trimmed, 12, QStringLiteral("Script"), QStringLiteral(">>>")) : name.trimmed());

    auto* item = new QListWidgetItem(style()->icon(Style::IconRole::Code, Style::UIScale::Large), QString());
    item->setSizeHint(d.list->gridSize());
    item->setData(Qt::UserRole, trimmed);
    item->setData(roles::shelf::scriptName, title);
    item->setToolTip(title);
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    if (!iconBytes.isEmpty()) {
        item->setData(roles::shelf::scriptIcon, iconBytes);
        item->setIcon(qt::pngBytesToIcon(iconBytes));
    }
    d.list->addItem(item);

    Q_EMIT d.shelf->changed();
}

void
ShelfWidgetPrivate::editScript(QListWidgetItem* item)
{
    if (!d.list || !item)
        return;

    const bool blocked = d.list->blockSignals(true);
    item->setText(item->data(roles::shelf::scriptName).toString());
    d.list->blockSignals(blocked);
    d.list->editItem(item);
}

void
ShelfWidgetPrivate::removeScript(QListWidgetItem* item)
{
    if (!d.list || !item)
        return;

    const int row = d.list->row(item);
    if (row < 0)
        return;

    delete d.list->takeItem(row);

    Q_EMIT d.shelf->changed();
}

void
ShelfWidgetPrivate::clear()
{
    d.list->clear();

    Q_EMIT d.shelf->changed();
}

int
ShelfWidgetPrivate::count() const
{
    return d.list ? d.list->count() : 0;
}

QString
ShelfWidgetPrivate::titleFromText(const QString& text, int maxLength, const QString& fallback,
                                  const QString& prefixToStrip)
{
    QString line;
    const QStringList lines = text.split('\n');
    for (QString l : lines) {
        l = l.trimmed();
        if (!l.isEmpty()) {
            line = l;
            break;
        }
    }
    if (line.isEmpty())
        return fallback;
    if (!prefixToStrip.isEmpty() && line.startsWith(prefixToStrip))
        line = line.mid(prefixToStrip.size()).trimmed();
    if (maxLength > 0 && line.length() > maxLength)
        line = line.left(maxLength).trimmed() + QStringLiteral("...");
    return line.isEmpty() ? fallback : line;
}

void
ShelfWidgetPrivate::itemIcon(QListWidgetItem* item, const QByteArray& iconBytes)
{
    if (iconBytes.isEmpty()) {
        item->setData(roles::shelf::scriptIcon, QByteArray());
        item->setIcon(style()->icon(Style::IconRole::Code));
    }
    else {
        item->setData(roles::shelf::scriptIcon, iconBytes);
        item->setIcon(qt::pngBytesToIcon(iconBytes));
    }

    if (d.shelf)
        Q_EMIT d.shelf->changed();
}

QVariantList
ShelfWidgetPrivate::toVariantList() const
{
    QVariantList scripts;
    if (!d.list)
        return scripts;

    for (int i = 0; i < d.list->count(); ++i) {
        const QListWidgetItem* item = d.list->item(i);
        QVariantMap m;
        m.insert("name", item->data(roles::shelf::scriptName).toString());
        m.insert("code", item->data(Qt::UserRole).toString());
        m.insert("icon", item->data(roles::shelf::scriptIcon).toByteArray());
        scripts.append(m);
    }
    return scripts;
}

void
ShelfWidgetPrivate::fromVariantList(const QVariantList& scripts)
{
    if (!d.list)
        return;

    d.list->clear();

    for (const QVariant& value : scripts) {
        const QVariantMap m = value.toMap();
        const QString name = m.value("name").toString().trimmed();
        const QString code = m.value("code").toString().trimmed();
        const QByteArray iconBytes = m.value("icon").toByteArray();
        if (code.isEmpty())
            continue;

        addScript(code, name, iconBytes);
    }

    Q_EMIT d.shelf->changed();
}

QString
ShelfWidgetPrivate::uniqueTitle(const QString& base, const QListWidgetItem* ignoreItem) const
{
    QString name = base.trimmed().isEmpty() ? QStringLiteral("Script") : base.trimmed();
    QString candidate = name;
    int index = 2;

    auto exists = [this, ignoreItem](const QString& value) {
        for (int i = 0; i < d.list->count(); ++i) {
            const QListWidgetItem* item = d.list->item(i);
            if (item == ignoreItem)
                continue;
            if (item->data(roles::shelf::scriptName).toString() == value)
                return true;
        }
        return false;
    };

    while (exists(candidate))
        candidate = QStringLiteral("%1 %2").arg(name).arg(index++);

    return candidate;
}

QListWidgetItem*
ShelfWidgetPrivate::itemAt(const QPoint& pos) const
{
    return d.list ? d.list->itemAt(pos) : nullptr;
}

void
ShelfWidgetPrivate::contextMenuEvent(const QPoint& pos)
{
    if (!d.shelf)
        return;

    Q_EMIT d.shelf->itemContextMenuRequested(pos, itemAt(pos));
}

ShelfWidget::ShelfWidget(QWidget* parent)
    : QWidget(parent)
    , p(new ShelfWidgetPrivate)
{
    p->d.shelf = this;
    p->init();
}

ShelfWidget::~ShelfWidget() {}

void
ShelfWidget::addScript(const QString& code, const QString& name)
{
    p->addScript(code, name);
}

void
ShelfWidget::editScript(QListWidgetItem* item)
{
    p->editScript(item);
}

void
ShelfWidget::removeScript(QListWidgetItem* item)
{
    p->removeScript(item);
}

void
ShelfWidget::clear()
{
    p->clear();
}

int
ShelfWidget::count() const
{
    return p->count();
}

QVariantList
ShelfWidget::toVariantList() const
{
    return p->toVariantList();
}

void
ShelfWidget::fromVariantList(const QVariantList& scripts)
{
    p->fromVariantList(scripts);
}

QSize
ShelfWidget::sizeHint() const
{
    const int iconSize = stageviz::style()->iconSize(Style::UIScale::Large);
    const int tilePadding = 10;
    const int height = iconSize + tilePadding * 2 + 2;
    return QSize(QWidget::sizeHint().width(), height);
}

QSize
ShelfWidget::minimumSizeHint() const
{
    const int iconSize = stageviz::style()->iconSize(Style::UIScale::Large);
    const int tilePadding = 10;
    const int height = iconSize + tilePadding * 2 + 2;
    return QSize(0, height);
}

bool
ShelfWidget::eventFilter(QObject* object, QEvent* event)
{
    return QWidget::eventFilter(object, event);
}

}  // namespace stageviz

#include "shelfwidget.moc"
