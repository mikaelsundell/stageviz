// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "style.h"
#include "qtutils.h"
#include <QApplication>
#include <QFile>
#include <QMetaEnum>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QSurfaceFormat>

namespace stageviz {

class StylePrivate {
public:
    StylePrivate();
    ~StylePrivate();
    void init();
    void updateTheme(Style::Theme theme);
    void updateColorSpace(const QColorSpace& colorSpace);
    void updateStylesheet();
    QColorSpace colorSpace() const;
    QColor color(Style::ColorRole role, Style::UIState state) const;
    int fontSize(Style::UIScale scale) const;
    QPixmap icon(Style::IconRole role, Style::UIScale scale, Style::UIState state) const;
    QString iconPath(Style::IconRole role) const;
    int iconSize(Style::UIScale scale) const;
    QString roleName(Style::ColorRole role) const;
    QString roleName(Style::IconRole role) const;
    QString roleName(Style::UIScale scale) const;

public:
    struct IconKey {
        int role;
        int scale;
        int state;
        int physicalSize;

        bool operator==(const IconKey& o) const
        {
            return role == o.role && scale == o.scale && state == o.state && physicalSize == o.physicalSize;
        }
    };

    struct Data {
        Style::Theme theme = Style::Theme::Dark;
        QHash<QString, QColor> palette;
        QHash<QString, QString> icons;
        QHash<QString, int> fontSizes;
        QHash<QString, int> iconSizes;
        mutable QHash<IconKey, QPixmap> pixmaps;
        QPointer<QObject> object;
    };

    Data d;
};

inline size_t
qHash(const StylePrivate::IconKey& k, size_t seed = 0)
{
    seed = ::qHash(k.role, seed);
    seed = ::qHash(k.scale, seed);
    seed = ::qHash(k.state, seed);
    seed = ::qHash(k.physicalSize, seed);
    return seed;
}

StylePrivate::StylePrivate() = default;
StylePrivate::~StylePrivate() = default;

void
StylePrivate::init()
{
    updateTheme(d.theme);
    updateColorSpace(QColorSpace::SRgb);
    updateStylesheet();
}

void
StylePrivate::updateColorSpace(const QColorSpace& colorSpace)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(colorSpace);
    QSurfaceFormat::setDefaultFormat(format);
}

void
StylePrivate::updateStylesheet()
{
    QFile file(":/style/resources/App.qss");
    if (!file.open(QFile::ReadOnly)) {
        qWarning() << "Failed to open QSS";
        return;
    }

    QString styleSheet = QString::fromUtf8(file.readAll());

    QRegularExpression regex(
        R"(\$([a-z0-9_]+(?:\.(?!(?:lightness|saturation)\()[a-z0-9_]+){0,2})(?:\.(lightness|saturation)\((\d+)\))?)",
        QRegularExpression::CaseInsensitiveOption);

    QString result;
    result.reserve(styleSheet.size());

    qsizetype lastIndex = 0;
    QRegularExpressionMatchIterator it = regex.globalMatch(styleSheet);

    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();

        const QString fullMatch = match.captured(0);
        const QString path = match.captured(1).toLower();
        const QString modifier = match.captured(2).toLower();
        const int factor = match.captured(3).isEmpty() ? 100 : match.captured(3).toInt();

        const QStringList parts = path.split('.');
        const QString group = parts.value(0);
        const QString roleStr = parts.value(1);
        const QString stateStr = parts.value(2);

        result.append(styleSheet.mid(lastIndex, match.capturedStart() - lastIndex));

        bool replaced = false;
        if (group == "color" && !roleStr.isEmpty()) {
            Style::ColorRole role = Style::ColorRole::Base;
            Style::UIState state = Style::UIState::Normal;
            {
                const QMetaEnum me = QMetaEnum::fromType<Style::ColorRole>();
                for (int i = 0; i < me.keyCount(); ++i) {
                    if (QString::fromLatin1(me.key(i)).compare(roleStr, Qt::CaseInsensitive) == 0) {
                        role = static_cast<Style::ColorRole>(me.value(i));
                        break;
                    }
                }
            }
            if (!stateStr.isEmpty()) {
                const QMetaEnum me = QMetaEnum::fromType<Style::UIState>();
                for (int i = 0; i < me.keyCount(); ++i) {
                    if (QString::fromLatin1(me.key(i)).compare(stateStr, Qt::CaseInsensitive) == 0) {
                        state = static_cast<Style::UIState>(me.value(i));
                        break;
                    }
                }
            }
            QColor mapped = color(role, state);
            if (mapped.isValid()) {
                if (modifier == "lightness") {
                    mapped = mapped.lighter(factor);
                }
                else if (modifier == "saturation") {
                    float h, s, l, a;
                    mapped.getHslF(&h, &s, &l, &a);
                    s = std::clamp(s * float(factor) / 100.0f, 0.0f, 1.0f);
                    mapped.setHslF(h, s, l, a);
                }
                result.append(QString("rgba(%1, %2, %3, %4)")
                                  .arg(mapped.red())
                                  .arg(mapped.green())
                                  .arg(mapped.blue())
                                  .arg(mapped.alpha()));

                replaced = true;
            }
        }
        if (!replaced && group == "font" && roleStr == "size" && !stateStr.isEmpty()) {
            const int size = d.fontSizes.value(stateStr, -1);
            if (size > 0) {
                result.append(QString::number(size) + "px");
                replaced = true;
            }
        }
        if (!replaced && group == "icon" && !roleStr.isEmpty()) {
            const QString iconPath = d.icons.value(roleStr);
            if (!iconPath.isEmpty()) {
                result.append(iconPath);
                replaced = true;
            }
        }
        if (!replaced) {
            result.append(fullMatch);
        }
        lastIndex = match.capturedEnd();
    }

    result.append(styleSheet.mid(lastIndex));
    qApp->setStyleSheet(result);
}

void
StylePrivate::updateTheme(Style::Theme theme)
{
    d.theme = theme;
    d.palette.clear();

    auto map = [&](Style::ColorRole role, QColor color) { d.palette[roleName(role)] = color; };
    if (theme == Style::Theme::Dark) {
        map(Style::ColorRole::Base, QColor::fromHsl(220, 6, 42));
        map(Style::ColorRole::BaseAlt, QColor::fromHsl(220, 6, 48));
        map(Style::ColorRole::Dock, QColor::fromHsl(220, 6, 56));
        map(Style::ColorRole::DockAlt, QColor::fromHsl(220, 6, 40));
        map(Style::ColorRole::Accent, QColor::fromHsl(220, 6, 20));
        map(Style::ColorRole::AccentAlt, QColor::fromHsl(220, 6, 24));
        map(Style::ColorRole::Text, QColor::fromHsl(0, 0, 220));
        map(Style::ColorRole::Highlight, QColor::fromHsl(216, 82, 80));
        map(Style::ColorRole::HighlightAlt, QColor::fromHsl(216, 60, 60));
        map(Style::ColorRole::Border, QColor::fromHsl(220, 3, 32));
        map(Style::ColorRole::BorderAlt, QColor::fromHsl(220, 3, 64));
        map(Style::ColorRole::Handle, QColor::fromHsl(0, 0, 150));
        map(Style::ColorRole::Progress, QColor::fromHsl(216, 82, 20));
        map(Style::ColorRole::Button, QColor::fromHsl(220, 6, 36));
        map(Style::ColorRole::ButtonAlt, QColor::fromHsl(220, 6, 64));
        map(Style::ColorRole::Render, QColor::fromHsl(210, 11, 35));
        map(Style::ColorRole::RenderAlt, QColor::fromHsl(210, 6, 25));
        map(Style::ColorRole::Selection, QColor::fromHsl(55, 220, 180));
        map(Style::ColorRole::SelectionAlt, QColor::fromHsl(55, 140, 120));
        map(Style::ColorRole::Warning, QColor(220, 170, 40));
        map(Style::ColorRole::Error, QColor(200, 50, 50));
    }
    else {
        map(Style::ColorRole::Base, QColor::fromHsl(0, 0, 210));
        map(Style::ColorRole::BaseAlt, QColor::fromHsl(0, 0, 208));
        map(Style::ColorRole::Dock, QColor::fromHsl(0, 0, 210));
        map(Style::ColorRole::DockAlt, QColor::fromHsl(0, 0, 180));
        map(Style::ColorRole::Accent, QColor::fromHsl(210, 10, 92));
        map(Style::ColorRole::AccentAlt, QColor::fromHsl(210, 10, 88));
        map(Style::ColorRole::Text, QColor::fromHsl(0, 0, 15));
        map(Style::ColorRole::Highlight, QColor::fromHsl(210, 90, 180));
        map(Style::ColorRole::HighlightAlt, QColor::fromHsl(210, 40, 220));
        map(Style::ColorRole::Border, QColor::fromHsl(0, 0, 200));
        map(Style::ColorRole::BorderAlt, QColor::fromHsl(0, 0, 180));
        map(Style::ColorRole::Handle, QColor::fromHsl(0, 0, 120));
        map(Style::ColorRole::Progress, QColor::fromHsl(210, 90, 45));
        map(Style::ColorRole::Button, QColor::fromHsl(0, 0, 180));
        map(Style::ColorRole::ButtonAlt, QColor::fromHsl(0, 0, 160));
        map(Style::ColorRole::Render, QColor::fromHsl(210, 11, 35));
        map(Style::ColorRole::RenderAlt, QColor::fromHsl(210, 6, 25));
        map(Style::ColorRole::Selection, QColor::fromHsl(55, 220, 180));
        map(Style::ColorRole::SelectionAlt, QColor::fromHsl(55, 140, 120));
        map(Style::ColorRole::Warning, QColor(180, 130, 30));
        map(Style::ColorRole::Error, QColor(180, 40, 40));
    }
    d.icons[roleName(Style::IconRole::BranchOpen)] = ":/icons/resources/BranchOpen.png";
    d.icons[roleName(Style::IconRole::BranchClosed)] = ":/icons/resources/BranchClosed.png";
    d.icons[roleName(Style::IconRole::Clear)] = ":/icons/resources/Clear.png";
    d.icons[roleName(Style::IconRole::Code)] = ":/icons/resources/Code.png";
    d.icons[roleName(Style::IconRole::Collapse)] = ":/icons/resources/Collapse.png";
    d.icons[roleName(Style::IconRole::Export)] = ":/icons/resources/Export.png";
    d.icons[roleName(Style::IconRole::ExportImage)] = ":/icons/resources/ExportImage.png";
    d.icons[roleName(Style::IconRole::Expand)] = ":/icons/resources/Expand.png";
    d.icons[roleName(Style::IconRole::FrameAll)] = ":/icons/resources/FrameAll.png";
    d.icons[roleName(Style::IconRole::Follow)] = ":/icons/resources/Follow.png";
    d.icons[roleName(Style::IconRole::Visible)] = ":/icons/resources/Visible.png";
    d.icons[roleName(Style::IconRole::Hidden)] = ":/icons/resources/Hidden.png";
    d.icons[roleName(Style::IconRole::Checked)] = ":/icons/resources/Checked.png";
    d.icons[roleName(Style::IconRole::Dropdown)] = ":/icons/resources/Dropdown.png";
    d.icons[roleName(Style::IconRole::Left)] = ":/icons/resources/Left.png";
    d.icons[roleName(Style::IconRole::Material)] = ":/icons/resources/Material.png";
    d.icons[roleName(Style::IconRole::Mesh)] = ":/icons/resources/Mesh.png";
    d.icons[roleName(Style::IconRole::Open)] = ":/icons/resources/Open.png";
    d.icons[roleName(Style::IconRole::PartiallyChecked)] = ":/icons/resources/PartiallyChecked.png";
    d.icons[roleName(Style::IconRole::Payload)] = ":/icons/resources/Payload.png";
    d.icons[roleName(Style::IconRole::Prim)] = ":/icons/resources/Prim.png";
    d.icons[roleName(Style::IconRole::Redo)] = ":/icons/resources/Redo.png";
    d.icons[roleName(Style::IconRole::Right)] = ":/icons/resources/Right.png";
    d.icons[roleName(Style::IconRole::Run)] = ":/icons/resources/Run.png";
    d.icons[roleName(Style::IconRole::Undo)] = ":/icons/resources/Undo.png";
    d.icons[roleName(Style::IconRole::Wireframe)] = ":/icons/resources/Wireframe.png";
    d.icons[roleName(Style::IconRole::Shaded)] = ":/icons/resources/Shaded.png";

    d.fontSizes[roleName(Style::UIScale::Small)] = 10;
    d.fontSizes[roleName(Style::UIScale::Medium)] = 11;
    d.fontSizes[roleName(Style::UIScale::Large)] = 14;

    d.iconSizes[roleName(Style::UIScale::Small)] = 16;
    d.iconSizes[roleName(Style::UIScale::Medium)] = 32;
    d.iconSizes[roleName(Style::UIScale::Large)] = 64;
}

QColorSpace
StylePrivate::colorSpace() const
{
    return QSurfaceFormat::defaultFormat().colorSpace();
}

QColor
StylePrivate::color(Style::ColorRole role, Style::UIState state) const
{
    QColor color = d.palette.value(roleName(role), QColor());
    if (state == Style::UIState::Disabled) {
        return color.darker(150);
    }
    return color;
}

int
StylePrivate::fontSize(Style::UIScale scale) const
{
    return d.fontSizes.value(roleName(scale), -1);
}

QPixmap
StylePrivate::icon(Style::IconRole role, Style::UIScale scale, Style::UIState state) const
{
    const qreal dpr = qt::devicePixelRatio();
    const int logicalSize = iconSize(scale);
    if (logicalSize <= 0)
        return QPixmap();

    const int physicalSize = qt::physicalPixelSize(logicalSize, dpr);
    IconKey key { int(role), int(scale), int(state), physicalSize };

    auto it = d.pixmaps.constFind(key);
    if (it != d.pixmaps.constEnd())
        return it.value();

    const QString path = iconPath(role);
    if (path.isEmpty())
        return QPixmap();

    QPixmap loaded(path);
    if (loaded.isNull())
        return QPixmap();

    QPixmap scaled = qt::scaledPixmap(loaded, logicalSize, Qt::KeepAspectRatio);
    d.pixmaps.insert(key, scaled);
    return scaled;
}

QString
StylePrivate::iconPath(Style::IconRole role) const
{
    return d.icons.value(roleName(role));
}

int
StylePrivate::iconSize(Style::UIScale scale) const
{
    return d.iconSizes.value(roleName(scale), -1);
}

QString
StylePrivate::roleName(Style::ColorRole role) const
{
    const QMetaEnum me = QMetaEnum::fromType<Style::ColorRole>();
    return QString::fromLatin1(me.valueToKey(role)).toLower();
}

QString
StylePrivate::roleName(Style::IconRole role) const
{
    const QMetaEnum me = QMetaEnum::fromType<Style::IconRole>();
    return QString::fromLatin1(me.valueToKey(role)).toLower();
}

QString
StylePrivate::roleName(Style::UIScale scale) const
{
    const QMetaEnum me = QMetaEnum::fromType<Style::UIScale>();
    return QString::fromLatin1(me.valueToKey(scale)).toLower();
}

Style::Style()
    : p(new StylePrivate())
{
    p->d.object = this;
    p->init();
}

Style::~Style() = default;

void
Style::setTheme(Theme theme)
{
    if (p->d.theme == theme)
        return;

    p->updateTheme(theme);
    Q_EMIT themeChanged(theme);
}

Style::Theme
Style::theme() const
{
    return p->d.theme;
}

QColor
Style::color(ColorRole role, UIState state) const
{
    return p->color(role, state);
}

QPixmap
Style::icon(IconRole role, UIScale scale, UIState state) const
{
    return p->icon(role, scale, state);
}

int
Style::fontSize(UIScale scale) const
{
    return p->fontSize(scale);
}

void
Style::setFontSize(UIScale scale, int size)
{
    if (size <= 0)
        return;

    const QString key = p->roleName(scale);
    if (p->d.fontSizes.value(key) == size)
        return;

    p->d.fontSizes[key] = size;
}

int
Style::iconSize(UIScale scale) const
{
    return p->iconSize(scale);
}

void
Style::setIconSize(UIScale scale, int size)
{
    if (size <= 0)
        return;

    const QString key = p->roleName(scale);
    if (p->d.iconSizes.value(key) == size)
        return;
}

void
Style::setColorSpace(const QColorSpace& colorSpace)
{
    p->updateColorSpace(colorSpace);
}

QColorSpace
Style::colorSpace() const
{
    return p->colorSpace();
}

}  // namespace stageviz
