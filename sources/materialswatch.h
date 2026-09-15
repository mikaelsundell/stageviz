// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QImage>
#include <QPixmap>
#include <QWidget>

namespace stageviz {

class MaterialSwatchPrivate;

/**
 * @class MaterialSwatch
 * @brief Resizable preview widget for rendered material swatches.
 *
 * MaterialSwatch displays a material preview image while preserving its aspect
 * ratio and provides a consistent background and border independent of layout
 * size.
 */
class MaterialSwatch : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Creates an empty preview widget.
     */
    explicit MaterialSwatch(QWidget* parent = nullptr);
    /**
     * @brief Releases the cached preview.
     */
    virtual ~MaterialSwatch();

    /**
     * @brief Replaces the preview image and schedules repainting.
     */
    void setImage(const QImage& image);
    /**
     * @brief Returns the current preview image.
     */
    QImage image() const;

    /**
     * @brief Replaces the preview using a Qt pixmap.
     */
    void setPixmap(const QPixmap& pixmap);
    /**
     * @brief Removes the current preview.
     */
    void clear();

    /**
     * @brief Returns the preferred preview size.
     */
    QSize sizeHint() const override;
    /**
     * @brief Returns the minimum preview size for layout negotiation.
     */
    QSize minimumSizeHint() const override;

protected:
    /**
     * @brief Draws the preview with its background and border.
     */
    void paintEvent(QPaintEvent* event) override;

private:
    QScopedPointer<MaterialSwatchPrivate> p;
};

}  // namespace stageviz
