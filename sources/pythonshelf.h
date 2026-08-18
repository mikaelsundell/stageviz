// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QWidget>
#include <memory>

namespace stageviz {

class PythonShelfPrivate;

/**
 * @class PythonShelf
 * @brief Tabbed shelf for storing and executing Python scripts.
 *
 * Each shelf tab contains a ShelfWidget. Shelf tabs and their scripts are
 * persisted through application settings and can be created, renamed,
 * reordered, exported, and removed.
 */
class PythonShelf : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Creates the Python shelf.
     * @param parent Parent widget.
     */
    explicit PythonShelf(QWidget* parent = nullptr);

    /**
     * @brief Destroys the Python shelf.
     */
    ~PythonShelf() override;

    /**
     * @brief Returns the preferred size of the Python shelf.
     * @return Preferred widget size.
     */
    QSize sizeHint() const override;

    /**
     * @brief Returns the minimum preferred size of the Python shelf.
     * @return Minimum preferred widget size.
     */
    QSize minimumSizeHint() const override;

private:
    std::unique_ptr<PythonShelfPrivate> p;
};

}  // namespace stageviz
