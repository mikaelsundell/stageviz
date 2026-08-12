// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "shelflist.h"
#include "application.h"
#include "mime.h"
#include "qtutils.h"
#include "roles.h"
#include "shelfwidget.h"
#include "style.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QBuffer>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QImageReader>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QStyledItemDelegate>

namespace stageviz {

class ShelfListPrivate {
public:
    ShelfListPrivate();
    ~ShelfListPrivate();
    void init();
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const;
    bool hasImageMime(const QMimeData* mime) const;
    bool hasScriptFileMime(const QMimeData* mime) const;
    QImage imageMimeData(const QMimeData* mime);
    QString scriptFileMimeData(const QMimeData* mime) const;
    QImage iconImage(const QImage& image) const;
    QImage centerCrop(const QImage& image) const;
    int dropRow(const QPoint& pos) const;
    void moveItem(int fromRow, int toRow);

public:
    class ShelfItemDelegate : public QStyledItemDelegate {
    public:
        explicit ShelfItemDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
        {}

        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            painter->save();
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);

            const auto* list = qobject_cast<const ShelfList*>(opt.widget);
            const bool isPressed = list && list->pressedIndex() == index;
            const bool isEnabled = (opt.state & QStyle::State_Enabled);

            const QIcon icon = opt.icon;
            opt.icon = QIcon();
            opt.text.clear();

            const int spacing = 2;
            QRect tileRect = opt.rect.adjusted(spacing, spacing, -spacing, -spacing);

            const QColor fill = isPressed ? style()->color(Style::ColorRole::ButtonAlt)
                                          : style()->color(Style::ColorRole::Button);

            painter->fillRect(tileRect, fill);

            const QRect iconRect = tileRect.adjusted(0, 0, 0, 0);
            const QPixmap pixmap = icon.pixmap(iconRect.size(), isEnabled ? QIcon::Normal : QIcon::Disabled,
                                               isPressed ? QIcon::On : QIcon::Off);

            if (!pixmap.isNull())
                painter->drawPixmap(iconRect, pixmap);

            painter->restore();
        }

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            Q_UNUSED(option);
            Q_UNUSED(index);

            if (const auto* list = qobject_cast<const ShelfList*>(parent()))
                return list->gridSize();

            return QSize(64, 64);
        }
    };

    struct Data {
        QModelIndex pressedIndex;
        QPoint pressPos;
        QPointer<ShelfList> list;
        bool dragStarted = false;
        int dragSourceRow = -1;
    };
    Data d;
};

ShelfListPrivate::ShelfListPrivate() {}

ShelfListPrivate::~ShelfListPrivate() {}

void
ShelfListPrivate::init()
{
    d.list->setViewMode(QListView::IconMode);
    d.list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d.list->setFlow(QListView::LeftToRight);
    d.list->setWrapping(false);
    d.list->setResizeMode(QListView::Adjust);
    d.list->setMovement(QListView::Static);
    d.list->setUniformItemSizes(true);
    d.list->setItemDelegate(new ShelfItemDelegate(d.list.data()));

    d.list->setDragDropMode(QAbstractItemView::DragDrop);
    d.list->setDefaultDropAction(Qt::CopyAction);
    d.list->setDragEnabled(true);
    d.list->setAcceptDrops(true);
    d.list->setDropIndicatorShown(true);

    d.list->setSelectionMode(QAbstractItemView::NoSelection);
    d.list->setContextMenuPolicy(Qt::CustomContextMenu);

    d.list->setFrameShape(QFrame::NoFrame);
    d.list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d.list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    d.list->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    d.list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const int iconSize = stageviz::style()->iconSize(Style::UIScale::Medium);
    const int padding = 6;
    const int spacing = 3;
    const int tileSize = iconSize + padding * 2;

    d.list->setSpacing(spacing);
    d.list->setIconSize(QSize(iconSize, iconSize));
    d.list->setGridSize(QSize(tileSize, tileSize));

    auto updateHeight = [this, tileSize, spacing]() {
        const bool hasHorizontalScroll = d.list->horizontalScrollBar()->maximum() > 0;
        const int scrollHeight = hasHorizontalScroll
                                     ? d.list->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, d.list)
                                     : 0;
        d.list->setFixedHeight(tileSize + spacing * 2 + scrollHeight);
    };

    QObject::connect(d.list->horizontalScrollBar(), &QScrollBar::rangeChanged, d.list.data(),
                     [updateHeight](int, int) { updateHeight(); });

    updateHeight();
    d.list->viewport()->setAcceptDrops(true);
}

QMimeData*
ShelfListPrivate::mimeData(const QList<QListWidgetItem*>& items) const
{
    if (items.isEmpty())
        return nullptr;

    const QString code = items.front()->data(Qt::UserRole).toString();
    if (code.isEmpty())
        return nullptr;

    auto* mime = new QMimeData();
    mime->setData(mime::script, code.toUtf8());
    mime->setText(code);
    return mime;
}

bool
ShelfListPrivate::hasImageMime(const QMimeData* mime) const
{
    if (!mime)
        return false;

    if (mime->hasImage())
        return true;

    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile())
                continue;

            QImageReader reader(url.toLocalFile());
            if (reader.canRead())
                return true;
        }
    }

    return false;
}

bool
ShelfListPrivate::hasScriptFileMime(const QMimeData* mime) const
{
    if (!mime || !mime->hasUrls())
        return false;

    const QList<QUrl> urls = mime->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile())
            continue;

        const QFileInfo info(url.toLocalFile());
        if (!info.isFile())
            continue;

        if (info.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0)
            return true;
    }

    return false;
}

QImage
ShelfListPrivate::imageMimeData(const QMimeData* mime)
{
    if (!mime)
        return QImage();

    if (mime->hasImage()) {
        const QVariant imageData = mime->imageData();
        if (imageData.canConvert<QImage>())
            return qvariant_cast<QImage>(imageData);
    }

    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        for (const QUrl& url : urls) {
            if (!url.isLocalFile())
                continue;

            QImageReader reader(url.toLocalFile());
            const QImage image = reader.read();
            if (!image.isNull())
                return image;
        }
    }

    return QImage();
}

QString
ShelfListPrivate::scriptFileMimeData(const QMimeData* mime) const
{
    if (!mime || !mime->hasUrls())
        return QString();

    const QList<QUrl> urls = mime->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile())
            continue;

        const QString filePath = url.toLocalFile();
        const QFileInfo info(filePath);

        if (!info.isFile())
            continue;

        if (info.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0)
            continue;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        return QString::fromUtf8(file.readAll());
    }

    return QString();
}

QImage
ShelfListPrivate::iconImage(const QImage& image) const
{
    if (image.isNull())
        return QImage();

    const int iconSize = stageviz::style()->iconSize(Style::UIScale::Medium);
    const int tilePadding = 6;
    const int logicalSize = iconSize + tilePadding * 2;

    if (logicalSize <= 0)
        return QImage();

    const QImage cropped = centerCrop(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return qt::scaledImage(cropped, logicalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage
ShelfListPrivate::centerCrop(const QImage& image) const
{
    if (image.isNull())
        return QImage();

    const int side = qMin(image.width(), image.height());
    const int x = (image.width() - side) / 2;
    const int y = (image.height() - side) / 2;

    return image.copy(x, y, side, side);
}


int
ShelfListPrivate::dropRow(const QPoint& pos) const
{
    if (!d.list)
        return -1;

    QListWidgetItem* targetItem = d.list->itemAt(pos);
    if (!targetItem)
        return d.list->count();

    int row = d.list->row(targetItem);
    if (row < 0)
        return d.list->count();

    const QRect rect = d.list->visualItemRect(targetItem);

    if (pos.x() > rect.center().x())
        ++row;

    return qBound(0, row, d.list->count());
}

void
ShelfListPrivate::moveItem(int fromRow, int toRow)
{
    if (!d.list)
        return;

    if (fromRow < 0 || fromRow >= d.list->count())
        return;

    toRow = qBound(0, toRow, d.list->count());

    if (toRow > fromRow)
        --toRow;

    if (toRow == fromRow)
        return;

    QListWidgetItem* item = d.list->takeItem(fromRow);
    if (!item)
        return;

    d.list->insertItem(toRow, item);
    d.list->setCurrentItem(item);
    d.list->viewport()->update();

    if (auto* widget = qobject_cast<ShelfWidget*>(d.list->parentWidget()))
        Q_EMIT widget->changed();
}

ShelfList::ShelfList(QWidget* parent)
    : QListWidget(parent)
    , p(new ShelfListPrivate())
{
    p->d.list = this;
    p->init();
}

ShelfList::~ShelfList() {}

QModelIndex
ShelfList::pressedIndex() const
{
    return p->d.pressedIndex;
}

QStringList
ShelfList::mimeTypes() const
{
    return { QString::fromLatin1(mime::script), QStringLiteral("text/plain"), QStringLiteral("text/uri-list") };
}

QMimeData*
ShelfList::mimeData(const QList<QListWidgetItem*>& items) const
{
    return p->mimeData(items);
}

Qt::DropActions
ShelfList::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

void
ShelfList::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->source() == this) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (event->mimeData()->hasFormat(mime::script) || event->mimeData()->hasText()
        || p->hasScriptFileMime(event->mimeData()) || p->hasImageMime(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }

    QListWidget::dragEnterEvent(event);
}

void
ShelfList::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->source() == this) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (event->mimeData()->hasFormat(mime::script) || event->mimeData()->hasText()
        || p->hasScriptFileMime(event->mimeData()) || p->hasImageMime(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }

    QListWidget::dragMoveEvent(event);
}

void
ShelfList::dropEvent(QDropEvent* event)
{
    const int sourceRow = p->d.dragSourceRow;

    p->d.pressedIndex = QModelIndex();
    p->d.dragStarted = false;
    p->d.dragSourceRow = -1;
    viewport()->update();

    if (event->source() == this) {
        const int targetRow = p->dropRow(event->position().toPoint());
        p->moveItem(sourceRow, targetRow);

        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (p->hasImageMime(event->mimeData())) {
        QListWidgetItem* targetItem = itemAt(event->position().toPoint());
        if (targetItem) {
            const QImage droppedImage = p->imageMimeData(event->mimeData());
            const QImage normalizedImage = p->iconImage(droppedImage);
            const QByteArray iconBytes = qt::imageToPngBytes(normalizedImage);

            if (!iconBytes.isEmpty()) {
                targetItem->setData(roles::shelf::scriptIcon, iconBytes);
                targetItem->setIcon(qt::pngBytesToIcon(iconBytes));
                viewport()->update();

                if (auto* widget = qobject_cast<ShelfWidget*>(parentWidget()))
                    Q_EMIT widget->changed();

                event->acceptProposedAction();
                return;
            }
        }
    }

    QString code;
    if (event->mimeData()->hasFormat(mime::script))
        code = QString::fromUtf8(event->mimeData()->data(mime::script));
    else if (p->hasScriptFileMime(event->mimeData()))
        code = p->scriptFileMimeData(event->mimeData());
    else if (event->mimeData()->hasText())
        code = event->mimeData()->text();

    code = qt::normalizeNewlines(code).trimmed();
    if (!code.isEmpty()) {
        if (auto* widget = qobject_cast<ShelfWidget*>(parentWidget()))
            widget->addScript(code);

        event->acceptProposedAction();
        return;
    }

    QListWidget::dropEvent(event);
}

void
ShelfList::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        p->d.pressedIndex = indexAt(event->pos());
        p->d.pressPos = event->pos();
        p->d.dragStarted = false;
        p->d.dragSourceRow = p->d.pressedIndex.isValid() ? p->d.pressedIndex.row() : -1;
        viewport()->update();
    }

    QListWidget::mousePressEvent(event);
}

void
ShelfList::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || !p->d.pressedIndex.isValid()) {
        QListWidget::mouseMoveEvent(event);
        return;
    }

    if ((event->pos() - p->d.pressPos).manhattanLength() < QApplication::startDragDistance()) {
        QListWidget::mouseMoveEvent(event);
        return;
    }

    QListWidgetItem* item = itemFromIndex(p->d.pressedIndex);
    if (!item) {
        QListWidget::mouseMoveEvent(event);
        return;
    }

    p->d.dragStarted = true;
    p->d.dragSourceRow = row(item);
    viewport()->update();

    QList<QListWidgetItem*> items;
    items.append(item);

    QMimeData* mime = p->mimeData(items);
    if (!mime)
        return;

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(item->icon().pixmap(iconSize()));
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
}

void
ShelfList::mouseReleaseEvent(QMouseEvent* event)
{
    const QModelIndex pressed = p->d.pressedIndex;
    const QModelIndex released = indexAt(event->pos());
    const bool dragStarted = p->d.dragStarted;

    p->d.pressedIndex = QModelIndex();
    p->d.dragStarted = false;
    p->d.dragSourceRow = -1;
    viewport()->update();

    QListWidget::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    if (dragStarted)
        return;

    if (!pressed.isValid() || pressed != released)
        return;

    QListWidgetItem* item = itemFromIndex(released);
    if (!item)
        return;

    if (auto* widget = qobject_cast<ShelfWidget*>(parentWidget()))
        Q_EMIT widget->itemActivated(item->data(Qt::UserRole).toString());

    event->accept();
}

void
ShelfList::leaveEvent(QEvent* event)
{
    if (p->d.dragStarted) {
        QListWidget::leaveEvent(event);
        return;
    }

    if (p->d.pressedIndex.isValid()) {
        p->d.pressedIndex = QModelIndex();
        p->d.dragSourceRow = -1;
        viewport()->update();
    }

    QListWidget::leaveEvent(event);
}

}  // namespace stageviz
