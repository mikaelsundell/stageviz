// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include <QImage>
#include <QObject>

namespace stageviz {

class MaterialRendererPrivate;

/**
 * @class MaterialRenderer
 * @brief Asynchronously renders material swatches for MaterialBrowser.
 *
 * A dedicated render thread owns the preview stage and RenderEngine for their
 * complete lifetime. The UI thread only queues immutable parameter requests and
 * receives QImage results.
 */
class MaterialRenderer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Creates the renderer and starts its dedicated preview worker.
     */
    explicit MaterialRenderer(QObject* parent = nullptr);
    /**
     * @brief Stops the worker and waits for its rendering resources to be released.
     */
    virtual ~MaterialRenderer();

    /**
     * @brief Requests a swatch render.
     * @param materialPath Material used as the cache/request key.
     * @param parameters Parameters to render.
     * @param forceRender Ignore any cached image for this request.
     */
    void request(const SdfPath& materialPath, const MaterialParameters& parameters, bool forceRender = false);

    /**
     * @brief Invalidates cached rendering for a material path.
     */
    void invalidate(const SdfPath& materialPath);
    /**
     * @brief Clears cached previews and pending renderer bookkeeping.
     */
    void clear();

Q_SIGNALS:
    /**
     * @brief Delivers a completed preview; materialPath identifies the original request.
     */
    void rendered(const QString& materialPath, const QImage& image);
    /**
     * @brief Reports a failed preview request and its diagnostic message.
     */
    void error(const QString& materialPath, const QString& message);

private:
    QScopedPointer<MaterialRendererPrivate> p;
};

}  // namespace stageviz
