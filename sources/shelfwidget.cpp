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
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStyle>
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
    QByteArray textIconBytes(const QString& text) const;
    QByteArray imageIconBytes(const QImage& image) const;
    QImage centerCrop(const QImage& image) const;
    QListWidgetItem* itemAt(const QPoint& pos) const;
    void itemIcon(QListWidgetItem* item, const QByteArray& iconBytes);
    void addIconMenu(QMenu* menu, QListWidgetItem* item);
    void generateIcon(QListWidgetItem* item);
    void chooseIcon(QListWidgetItem* item);
    void resetIcon(QListWidgetItem* item);
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
    layout->setContentsMargins(6, 4, 0, 0);
    layout->addWidget(d.list);

    QObject::connect(d.list, &QWidget::customContextMenuRequested, this, &ShelfWidgetPrivate::contextMenuEvent);
    QObject::connect(d.list->model(), &QAbstractItemModel::rowsInserted, this, [this]() { Q_EMIT d.shelf->changed(); });
    QObject::connect(d.list->model(), &QAbstractItemModel::rowsRemoved, this, [this]() { Q_EMIT d.shelf->changed(); });
    QObject::connect(d.list->model(), &QAbstractItemModel::modelReset, this, [this]() { Q_EMIT d.shelf->changed(); });
    QObject::connect(d.list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!item)
            return;

        QString editedName = item->data(roles::shelf::scriptName).toString().trimmed();
        const QString newName = uniqueTitle(editedName.isEmpty() ? QStringLiteral("Script") : editedName, item);
        const bool blocked = d.list->blockSignals(true);

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

    auto* item = new QListWidgetItem(style()->icon(Style::IconRole::Code, Style::UIScale::Medium), QString());
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
    if (!d.list)
        return;

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

    if (!prefixToStrip.isEmpty() && line.startsWith(prefixToStrip)) {
        line = line.mid(prefixToStrip.size()).trimmed();
    }
    if (maxLength > 0 && line.length() > maxLength) {
        line = line.left(maxLength).trimmed() + QStringLiteral("...");
    }
    return line.isEmpty() ? fallback : line;
}

QImage
ShelfWidgetPrivate::centerCrop(const QImage& image) const
{
    if (image.isNull())
        return QImage();

    const int side = qMin(image.width(), image.height());
    const int x = (image.width() - side) / 2;
    const int y = (image.height() - side) / 2;
    return image.copy(x, y, side, side);
}

QByteArray
ShelfWidgetPrivate::imageIconBytes(const QImage& image) const
{
    if (image.isNull())
        return QByteArray();

    const int iconSize = style()->iconSize(Style::UIScale::Medium);
    const int tilePadding = 6;
    const int logicalSize = iconSize + tilePadding * 2;

    if (logicalSize <= 0)
        return QByteArray();

    const QImage cropped = centerCrop(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage scaled = qt::scaledImage(cropped, logicalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return qt::imageToPngBytes(scaled);
}

QByteArray
ShelfWidgetPrivate::textIconBytes(const QString& input) const
{
    const QString text = input.trimmed().left(5);

    if (text.isEmpty())
        return QByteArray();

    const int iconSize = style()->iconSize(Style::UIScale::Medium);
    const int tilePadding = 6;
    const int size = iconSize + tilePadding * 2;

    if (size <= 0)
        return QByteArray();

    const qreal dpr = qt::devicePixelRatio();
    const int physicalSize = qt::physicalPixelSize(size, dpr);

    QImage image(physicalSize, physicalSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(style()->color(Style::ColorRole::BaseAlt));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(style()->color(Style::ColorRole::Text));

    QFont font = painter.font();
    font.setWeight(QFont::Medium);

    if (text.size() == 1)
        font.setPixelSize(qRound(size * 0.42));
    else if (text.size() == 2)
        font.setPixelSize(qRound(size * 0.36));
    else if (text.size() == 3)
        font.setPixelSize(qRound(size * 0.30));
    else if (text.size() == 4)
        font.setPixelSize(qRound(size * 0.25));
    else
        font.setPixelSize(qRound(size * 0.22));

    const int horizontalPadding = qMax(4, qRound(size * 0.12));
    const int availableWidth = size - horizontalPadding * 2;

    while (font.pixelSize() > 8) {
        const QFontMetrics metrics(font);

        if (metrics.horizontalAdvance(text) <= availableWidth)
            break;

        font.setPixelSize(font.pixelSize() - 1);
    }

    painter.setFont(font);
    painter.drawText(QRectF(0.0, 0.0, size, size), Qt::AlignCenter, text);
    painter.end();

    return qt::imageToPngBytes(image);
}

void
ShelfWidgetPrivate::generateIcon(QListWidgetItem* item)
{
    if (!item || !d.shelf)
        return;

    QInputDialog dialog(d.shelf.data());
    dialog.setWindowTitle(tr("Generate Icon"));
    dialog.setLabelText(tr("Text (1-5 characters):"));
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setTextValue(item->data(roles::shelf::scriptIconText).toString());

    if (QLineEdit* lineEdit = dialog.findChild<QLineEdit*>()) {
        lineEdit->setMaxLength(5);
        lineEdit->selectAll();
    }

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString text = dialog.textValue().trimmed();

    if (text.isEmpty())
        return;

    const QByteArray iconBytes = textIconBytes(text);

    if (iconBytes.isEmpty())
        return;

    item->setData(roles::shelf::scriptIconText, text);
    itemIcon(item, iconBytes);
}

void
ShelfWidgetPrivate::chooseIcon(QListWidgetItem* item)
{
    if (!item || !d.shelf)
        return;

    const QString filename
        = QFileDialog::getOpenFileName(d.shelf.data(), tr("Choose Icon"), QString(),
                                       tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));

    if (filename.isEmpty())
        return;

    QImageReader reader(filename);
    reader.setAutoTransform(true);

    const QImage image = reader.read();
    const QByteArray iconBytes = imageIconBytes(image);

    if (!iconBytes.isEmpty()) {
        item->setData(roles::shelf::scriptIconText, QString());
        itemIcon(item, iconBytes);
    }
}

void
ShelfWidgetPrivate::resetIcon(QListWidgetItem* item)
{
    if (!item)
        return;

    item->setData(roles::shelf::scriptIconText, QString());
    itemIcon(item, QByteArray());
}

void
ShelfWidgetPrivate::addIconMenu(QMenu* menu, QListWidgetItem* item)
{
    if (!menu || !item)
        return;

    QMenu* iconMenu = menu->addMenu(tr("Icon"));
    QAction* generateAction = iconMenu->addAction(tr("Generate..."));
    iconMenu->addSeparator();
    QAction* chooseAction = iconMenu->addAction(tr("Choose Image..."));
    QAction* resetAction = iconMenu->addAction(tr("Reset"));
    // connect
    QObject::connect(generateAction, &QAction::triggered, d.shelf.data(), [this, item]() { generateIcon(item); });
    QObject::connect(chooseAction, &QAction::triggered, d.shelf.data(), [this, item]() { chooseIcon(item); });
    QObject::connect(resetAction, &QAction::triggered, d.shelf.data(), [this, item]() { resetIcon(item); });
}

void
ShelfWidgetPrivate::itemIcon(QListWidgetItem* item, const QByteArray& iconBytes)
{
    if (!item)
        return;

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
        m.insert("iconText", item->data(roles::shelf::scriptIconText).toString());
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
        const QString iconText = m.value("iconText").toString().left(5);

        if (code.isEmpty())
            continue;

        addScript(code, name, iconBytes);

        if (!iconText.isEmpty() && d.list->count() > 0)
            d.list->item(d.list->count() - 1)->setData(roles::shelf::scriptIconText, iconText);
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
        if (!d.list)
            return false;

        for (int i = 0; i < d.list->count(); ++i) {
            const QListWidgetItem* item = d.list->item(i);

            if (item == ignoreItem)
                continue;

            if (item->data(roles::shelf::scriptName).toString() == value)
                return true;
        }

        return false;
    };

    while (exists(candidate)) {
        candidate = QStringLiteral("%1 %2").arg(name).arg(index++);
    }
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
ShelfWidget::addIconMenu(QMenu* menu, QListWidgetItem* item)
{
    p->addIconMenu(menu, item);
}

void
ShelfWidget::generateIcon(QListWidgetItem* item)
{
    p->generateIcon(item);
}

void
ShelfWidget::chooseIcon(QListWidgetItem* item)
{
    p->chooseIcon(item);
}

void
ShelfWidget::resetIcon(QListWidgetItem* item)
{
    p->resetIcon(item);
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
    const int iconSize = stageviz::style()->iconSize(Style::UIScale::Medium);
    const int padding = 6;
    const int spacing = 3;
    const int topMargin = 8;
    const int height = iconSize + padding * 2 + spacing * 2 + topMargin;
    return QSize(QWidget::sizeHint().width(), height);
}

QSize
ShelfWidget::minimumSizeHint() const
{
    const int iconSize = stageviz::style()->iconSize(Style::UIScale::Medium);
    const int padding = 6;
    const int spacing = 3;
    const int topMargin = 8;
    const int height = iconSize + padding * 2 + spacing * 2 + topMargin;
    return QSize(0, height);
}

bool
ShelfWidget::eventFilter(QObject* object, QEvent* event)
{
    return QWidget::eventFilter(object, event);
}

}  // namespace stageviz

#include "shelfwidget.moc"
