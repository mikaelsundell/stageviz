// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QWidget>

namespace stageviz {

class MaterialSwatchPrivate;

/**
 * @class MaterialSwatch
 * @brief Resizable preview widget for rendered material swatches.
 *
 * MaterialSwatch displays a material preview image while preserving its aspect
 * ratio and provides a consistent background and border independent of layout
 * size. When a material path is set, the swatch can also be dragged onto scene
 * geometry using the same material MIME payload as MaterialBrowser.
 */
class MaterialSwatch : public QWidget {
    Q_OBJECT
public:
    explicit MaterialSwatch(QWidget* parent = nullptr);
    virtual ~MaterialSwatch();

    void setImage(const QImage& image);
    QImage image() const;

    void setMaterial(const QImage& image, const QString& materialPath);

    void setPixmap(const QPixmap& pixmap);
    void clear();

    void setMaterialPath(const QString& path);
    QString materialPath() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QScopedPointer<MaterialSwatchPrivate> p;
};

}  // namespace stageviz
