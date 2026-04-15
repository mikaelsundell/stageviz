// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QScopedPointer>
#include <QSlider>

namespace stageviz {

class SliderPrivate;

/**
 * @class Slider
 * @brief Custom slider with overridden tick rendering.
 *
 * Provides a QSlider implementation where the tick marks are
 * rendered manually, allowing full control over appearance
 * independent of the Qt style engine.
 */
class Slider : public QSlider {
    Q_OBJECT
public:
    /**
     * @brief Constructs the slider.
     *
     * @param parent Optional parent widget.
     */
    explicit Slider(QWidget* parent = nullptr);

    /**
     * @brief Destroys the slider.
     */
    ~Slider() override;

protected:
    /**
     * @brief Paints the slider and custom tick marks.
     */
    void paintEvent(QPaintEvent* event) override;

private:
    QScopedPointer<SliderPrivate> p;
};

}  // namespace stageviz
