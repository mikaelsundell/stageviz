// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <any>
#include <functional>
#include <optional>
#include <pxr/usd/usd/stage.h>

namespace stageviz::fileformats {
/**
 * @brief Describes a native or language-adapted file converter.
 *
 * Options are opaque provider-owned data passed from settings to conversion.
 * A missing settings result cancels the operation; noninteractive calls pass an
 * empty std::any. Loaders return USD stages; savers receive isolated USD stages
 * and write external files at the supplied destinations. Callbacks may throw
 * std::exception on failure. All registry operations run on the application thread.
 */
struct Format {
    using Options = std::any;
    using Load = std::function<pxr::UsdStageRefPtr(const QString&, const Options&)>;
    using Save = std::function<void(const pxr::UsdStageRefPtr&, const QString&, const Options&)>;
    using Settings = std::function<std::optional<Options>(const QString&, const QString&)>;
    QString name;
    QStringList extensions;
    Load loader;
    Save saver;
    Settings settings;
    // Optional identity used to release only this provider's registrations.
    const void* provider = nullptr;
};

/**
 * @brief Registers a converter, rejecting invalid or conflicting names/extensions.
 */
void
registerFormat(Format format);

/**
 * @brief Removes a registration without invalidating existing imported USD stages.
 */
bool
unregisterFormat(const QString& name);

/**
 * @brief Releases registrations belonging to a non-null provider identity.
 */
void
unregisterProvider(const void* provider);

/**
 * @brief Returns metadata copies with callbacks omitted.
 */
struct Info {
    QString name;
    QStringList extensions;
    bool readable = false;
    bool writable = false;
};

/**
 * @brief Lists registered converters in registration order.
 */
QList<Info>
formats();

/**
 * @brief Releases converter callbacks before providers such as Python shut down.
 */
void
clear();

/**
 * @brief Tests whether a filename uses an extension registered with OpenUSD.
 */
bool
isNative(const QString& filename);

/**
 * @brief Tests OpenUSD metadata and registered Stageviz converter capabilities.
 */
bool
supports(const QString& filename, bool writing = false);

/**
 * @brief Gets the default output extension from USD's registered format metadata.
 *
 * @param text Prefer USD's text format for newly created editable layers.
 */
QString
defaultExtension(bool text = false);

/**
 * @brief Builds file-dialog filters from OpenUSD and the Stageviz registry.
 *
 * @param writing Include only formats advertising write support; otherwise use read support.
 * @param nativeOnly Include only formats registered with OpenUSD.
 */
QString
filters(bool writing = false, bool nativeOnly = false);

/**
 * @brief Returns an isolated anonymous snapshot of the composed stage, entirely in memory.
 *
 * Includes session opinions and anchors resolved asset paths. Throws for an invalid stage.
 */
pxr::UsdStageRefPtr
snapshot(const pxr::UsdStageRefPtr& stage);
/**
 * @brief Imports an external file to an isolated anonymous USD stage, or returns null on cancellation.
 *
 * Native USD files open normally. Throws on conversion or validation failure.
 */
pxr::UsdStageRefPtr
read(const QString& filename, bool interactive = false, const QString& operation = QStringLiteral("open"));
/**
 * @brief Passes an isolated in-memory USD snapshot to a saver and atomically publishes its external file.
 *
 * Returns false on cancellation and throws on conversion or validation failure.
 */
bool
write(const pxr::UsdStageRefPtr& stage, const QString& filename, bool interactive = false);
}  // namespace stageviz::fileformats
