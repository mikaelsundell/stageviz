// SPDX-License-Identifier: BSD-3-Clause
#include "fileformats.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QThread>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/usdFileFormat.h>
#include <pxr/usd/sdf/usdaFileFormat.h>
#include <pxr/usd/usd/stage.h>
#include <stdexcept>

namespace stageviz::fileformats {
namespace {
    struct Registry {
        QList<Format> formats;
        bool busy = false;
    };

    Registry& registry()
    {
        static Registry value;
        return value;
    }

    void requireThread()
    {
        if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread())
            throw std::runtime_error("File-format registry requires the application thread");
    }

    QStringList nativeExtensions(bool writing)
    {
        QStringList result;
        for (const std::string& extension : pxr::SdfFileFormat::FindAllFileFormatExtensions()) {
            const bool supported = writing ? pxr::SdfFileFormat::FormatSupportsWriting(extension)
                                           : pxr::SdfFileFormat::FormatSupportsReading(extension);
            if (supported)
                result.append(QString::fromStdString(extension));
        }
        // USD remains the default output while other registered formats are discovered.
        const QString preferred = defaultExtension();
        if (result.removeOne(preferred))
            result.prepend(preferred);
        return result;
    }

    Format find(const QString& path, bool writing)
    {
        const QString extension = QFileInfo(path).suffix().toLower();
        for (const Format& format : registry().formats)
            if (format.extensions.contains(extension) && (writing ? bool(format.saver) : bool(format.loader)))
                return format;
        throw std::invalid_argument(
            (QString(writing ? "No saver registered for " : "No loader registered for ") + path).toStdString());
    }

    void requireIdle()
    {
        requireThread();
        if (registry().busy)
            throw std::runtime_error("Recursive file-format conversion is not supported");
    }

    QString absolutePath(const QString& path)
    {
        const QString expanded = path.startsWith("~/") ? QDir::home().filePath(path.mid(2)) : path;
        const QFileInfo info(expanded);
        return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    }

    std::optional<Format::Options> settings(const Format& format, const QString& path, const QString& operation,
                                            bool interactive)
    {
        return interactive && format.settings ? format.settings(path, operation)
                                              : std::optional<Format::Options>(Format::Options());
    }
}  // namespace

void
registerFormat(Format format)
{
    requireThread();
    if (format.name.trimmed().isEmpty() || format.name.contains(QRegularExpression("[();*?\\r\\n]")))
        throw std::invalid_argument("A nonempty file-format name without filter punctuation is required");
    QStringList extensions;
    for (QString extension : format.extensions) {
        extension = extension.toLower();
        while (extension.startsWith('.'))
            extension.remove(0, 1);
        if (!QRegularExpression("^[a-z0-9][a-z0-9_-]*$").match(extension).hasMatch())
            throw std::invalid_argument("Use simple file extensions without wildcards");
        if (pxr::SdfFileFormat::FindAllFileFormatExtensions().count(extension.toStdString()))
            throw std::invalid_argument("Registered USD file-format extensions cannot be replaced");
        if (!extensions.contains(extension))
            extensions.append(extension);
    }
    if (extensions.isEmpty() || (!format.loader && !format.saver))
        throw std::invalid_argument("File formats require extensions and a loader or saver");
    for (const Format& existing : registry().formats) {
        if (existing.name == format.name)
            throw std::invalid_argument("File-format name is already registered");
        for (const QString& extension : extensions)
            if (existing.extensions.contains(extension))
                throw std::invalid_argument("File-format extension is already registered");
    }
    format.extensions = extensions;
    registry().formats.append(std::move(format));
}

bool
unregisterFormat(const QString& name)
{
    requireThread();
    auto& entries = registry().formats;
    for (qsizetype i = 0; i < entries.size(); ++i)
        if (entries[i].name == name) {
            entries.removeAt(i);
            return true;
        }
    return false;
}

void
unregisterProvider(const void* provider)
{
    requireThread();
    if (!provider)
        return;
    auto& entries = registry().formats;
    for (qsizetype i = entries.size(); i > 0; --i)
        if (entries[i - 1].provider == provider)
            entries.removeAt(i - 1);
}

QList<Info>
formats()
{
    requireThread();
    QList<Info> result;
    for (const Format& format : registry().formats)
        result.append({ format.name, format.extensions, bool(format.loader), bool(format.saver) });
    return result;
}

void
clear()
{
    requireThread();
    registry().formats.clear();
}

bool
isNative(const QString& filename)
{
    return pxr::SdfFileFormat::FindAllFileFormatExtensions().count(QFileInfo(filename).suffix().toLower().toStdString())
           != 0;
}

bool
supports(const QString& filename, bool writing)
{
    requireThread();
    if (isNative(filename)) {
        const std::string extension = QFileInfo(filename).suffix().toLower().toStdString();
        return writing ? pxr::SdfFileFormat::FormatSupportsWriting(extension)
                       : pxr::SdfFileFormat::FormatSupportsReading(extension);
    }
    const QString extension = QFileInfo(filename).suffix().toLower();
    for (const Format& format : registry().formats)
        if (format.extensions.contains(extension) && (writing ? bool(format.saver) : bool(format.loader)))
            return true;
    return false;
}

QString
defaultExtension(bool text)
{
    const auto format = pxr::SdfFileFormat::FindById(text ? pxr::SdfUsdaFileFormatTokens->Id
                                                          : pxr::SdfUsdFileFormatTokens->Id);
    if (format && format->SupportsWriting())
        return QString::fromStdString(format->GetPrimaryFileExtension());
    for (const auto& extension : pxr::SdfFileFormat::FindAllFileFormatExtensions())
        if (pxr::SdfFileFormat::FormatSupportsWriting(extension))
            return QString::fromStdString(extension);
    return {};
}

QString
filters(bool writing, bool nativeOnly)
{
    requireThread();
    QStringList result;
    QStringList patterns;
    auto append = [&](const QString& name, const QStringList& extensions) {
        QStringList entry;
        for (const QString& extension : extensions)
            entry.append("*." + extension);
        patterns.append(entry);
        result.append(QString("%1 (%2)").arg(name, entry.join(' ')));
    };
    const QStringList extensions = nativeExtensions(writing);
    if (!extensions.isEmpty())
        append("USD Files", extensions);
    if (!nativeOnly)
        for (const Format& format : registry().formats)
            if (writing ? bool(format.saver) : bool(format.loader))
                append(format.name, format.extensions);
    if (!writing && result.size() > 1)
        result.prepend(QString("All Supported Files (%1)").arg(patterns.join(' ')));
    return result.join(";;");
}

pxr::UsdStageRefPtr
snapshot(const pxr::UsdStageRefPtr& source)
{
    if (!source)
        throw std::invalid_argument("A valid USD stage is required");
    const auto layer = source->Flatten();
    if (!layer)
        throw std::runtime_error("Could not snapshot USD stage");
    const auto stage = pxr::UsdStage::Open(layer, source->GetPathResolverContext());
    if (!stage)
        throw std::runtime_error("Could not open in-memory USD snapshot");
    return stage;
}

pxr::UsdStageRefPtr
read(const QString& filename, bool interactive, const QString& operation)
{
    requireIdle();
    const QString source = absolutePath(filename);
    if (isNative(filename)) {
        if (!supports(filename))
            throw std::invalid_argument("USD file format does not support reading");
        return pxr::UsdStage::Open(source.toStdString());
    }
    const Format format = find(source, false);
    if (!QFileInfo(source).isFile())
        throw std::runtime_error("Input file does not exist: " + source.toStdString());
    QScopedValueRollback<bool> busy(registry().busy, true);
    const auto options = settings(format, source, operation, interactive);
    if (!options)
        return {};
    const auto loaded = format.loader(source, *options);
    if (!loaded)
        throw std::invalid_argument("Loader must return a valid USD stage");
    return snapshot(loaded);
}

bool
write(const pxr::UsdStageRefPtr& source, const QString& filename, bool interactive)
{
    requireIdle();
    const QString destination = absolutePath(filename);
    const Format format = find(destination, true);
    QScopedValueRollback<bool> busy(registry().busy, true);
    const auto isolated = snapshot(source);
    const auto options = settings(format, destination, "export", interactive);
    if (!options)
        return false;

    QTemporaryDir directory(QFileInfo(destination).absolutePath() + "/.stageviz-XXXXXX");
    if (!directory.isValid())
        throw std::runtime_error("Could not create saver working directory");

    const QString output = directory.filePath(QFileInfo(destination).fileName());
    format.saver(isolated, output, *options);

    if (!QFileInfo(output).isFile())
        throw std::runtime_error("Saver did not produce its output file");

    const QStringList entries
        = QDir(directory.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

    if (entries != QStringList { QFileInfo(output).fileName() })
        throw std::invalid_argument("Savers must produce a single self-contained file");

    QFile converted(output);
    QSaveFile published(destination);
    if (!converted.open(QIODevice::ReadOnly) || !published.open(QIODevice::WriteOnly))
        throw std::runtime_error("Could not open converter output or destination");

    while (!converted.atEnd()) {
        const QByteArray block = converted.read(1024 * 1024);
        if (converted.error() != QFile::NoError || published.write(block) != block.size())
            throw std::runtime_error("Could not publish converter output");
    }
    if (!published.commit())
        throw std::runtime_error("Could not commit converter output");
    return true;
}
}  // namespace stageviz::fileformats
