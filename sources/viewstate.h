// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QObject>
#include <QScopedPointer>

namespace stageviz {

class ViewCamera;
class ViewStatePrivate;

/**
 * @class ViewState
 * @brief Shared viewport state for the USD viewer.
 *
 * Provides a central state object for viewport-related data such as
 * the interactive camera. The state is owned by the session and shared
 * with view widgets through ViewContext.
 *
 * Internally the view camera state can be converted to a USD compatible
 * @c GfCamera for rendering.
 */
class ViewState : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a ViewState.
     *
     * @param parent Optional parent object.
     */
    explicit ViewState(QObject* parent = nullptr);

    /**
     * @brief Destroys the ViewState instance.
     */
    ~ViewState() override;

    /** @name Camera */
    ///@{

    /**
     * @brief Returns the interactive view camera.
     */
    ViewCamera* camera() const;

    ///@}

private:
    Q_DISABLE_COPY_MOVE(ViewState)
    QScopedPointer<ViewStatePrivate> p;
};

}  // namespace stageviz
