// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialswatch.h"
#include "application.h"
#include "style.h"
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
        QSize sizeHint = QSize(256, 256);
        QSize minimumSizeHint = QSize(96, 96);
        int margin = 8;
        QPointer<MaterialSwatch> swatch;
    };
    Data d;
};

void
MaterialSwatchPrivate::init()
{
    d.swatch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d.swatch->setMinimumSize(d.minimumSizeHint);
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
