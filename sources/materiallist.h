// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QListWidget>

namespace stageviz {

/**
 * @class MaterialList
 * @brief QListWidget used by the material browser.
 *
 * Provides the material browser with a dedicated list type so the icon view
 * can be promoted in Designer and configured consistently in one place.
 */
class MaterialList : public QListWidget {
    Q_OBJECT
public:
    /**
     * @brief Creates the material list.
     * @param parent Parent widget.
     */
    explicit MaterialList(QWidget* parent = nullptr);

    /**
     * @brief Destroys the material list.
     */
    ~MaterialList() override;
};

}  // namespace stageviz
