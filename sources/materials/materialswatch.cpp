// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialswatch.h"
#include "application.h"
#include "mime.h"
#include "style.h"
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>

namespace stageviz {

class MaterialSwatchPrivate {
public:
    void init();

public:
    struct Data {
        QImage image;
        QString materialPath;
        QPoint dragStartPosition;
        QSize sizeHint = QSize(256, 256);
        QSize minimumSizeHint = QSize(96, 96);
        int margin = 8;
        bool materialDragActive = false;
        QPointer<MaterialSwatch> swatch;
    };
    Data d;
};

void
MaterialSwatchPrivate::init()
{
    d.swatch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d.swatch->setMinimumSize(d.minimumSizeHint);
    d.swatch->setMouseTracking(true);
}

MaterialSwatch::MaterialSwatch(QWidget* parent)
    : QWidget(parent)
    , p(new MaterialSwatchPrivate())
{
    p->d.swatch = this;
    p->init();
}

MaterialSwatch::~MaterialSwatch() = default;

void
MaterialSwatch::setImage(const QImage& image)
{
    p->d.image = image;
    update();
}

QImage
MaterialSwatch::image() const
{
    return p->d.image;
}

void
MaterialSwatch::setMaterial(const QImage& image, const QString& materialPath)
{
    p->d.image = image;
    p->d.materialPath = materialPath.trimmed();
    p->d.dragStartPosition = QPoint();
    p->d.materialDragActive = false;
    setCursor(p->d.materialPath.isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
    update();
}

void
MaterialSwatch::setPixmap(const QPixmap& pixmap)
{
    setImage(pixmap.toImage());
}

void
MaterialSwatch::clear()
{
    p->d.image = QImage();
    update();
}

void
MaterialSwatch::setMaterialPath(const QString& path)
{
    p->d.materialPath = path.trimmed();
    setCursor(p->d.materialPath.isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
}

QString
MaterialSwatch::materialPath() const
{
    return p->d.materialPath;
}

QSize
MaterialSwatch::sizeHint() const
{
    return p->d.sizeHint;
}

QSize
MaterialSwatch::minimumSizeHint() const
{
    return p->d.minimumSizeHint;
}

void
MaterialSwatch::mousePressEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton) {
        if (!p->d.materialPath.isEmpty()) {
            p->d.dragStartPosition = event->position().toPoint();
            p->d.materialDragActive = false;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void
MaterialSwatch::mouseMoveEvent(QMouseEvent* event)
{
    if (!event || p->d.materialDragActive || p->d.materialPath.isEmpty() || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if ((event->position().toPoint() - p->d.dragStartPosition).manhattanLength() < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    p->d.materialDragActive = true;

    auto* mimeData = new QMimeData();
    mimeData->setData(mime::material, p->d.materialPath.toUtf8());

    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);


    drag->exec(Qt::CopyAction, Qt::CopyAction);

    p->d.materialDragActive = false;
    p->d.dragStartPosition = QPoint();
    setCursor(Qt::OpenHandCursor);

    event->accept();
}

void
MaterialSwatch::mouseReleaseEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton) {
        p->d.materialDragActive = false;
        p->d.dragStartPosition = QPoint();
        setCursor(p->d.materialPath.isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void
MaterialSwatch::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QColor backgroundColor = stageviz::style()->color(Style::ColorRole::Render);
    const QColor border = stageviz::style()->color(Style::ColorRole::BorderAlt);

    painter.fillRect(rect(), backgroundColor);

    const QRect frame = rect().adjusted(0, 0, -1, -1);
    painter.setPen(border);
    painter.drawRect(frame);

    if (p->d.image.isNull())
        return;

    const QRect available = rect().adjusted(p->d.margin, p->d.margin, -p->d.margin, -p->d.margin);

    QSize target = p->d.image.size();
    target.scale(available.size(), Qt::KeepAspectRatio);

    const QRect imageRect(available.center().x() - target.width() / 2, available.center().y() - target.height() / 2,
                          target.width(), target.height());

    painter.drawImage(imageRect, p->d.image);
}

}  // namespace stageviz
