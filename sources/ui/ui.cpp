// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "ui.h"
#include "os.h"
#include "qtutils.h"
#include <QApplication>
#include <QFile>
#include <QMetaEnum>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QSurfaceFormat>
#include <algorithm>
#include <cmath>

namespace stageviz {

class UIPrivate {
public:
    UIPrivate();
    ~UIPrivate();
    void init();
    void updateColorSpace(const QColorSpace& colorSpace);
    void updateStylesheet();
    void updateTheme();
    QColorSpace colorSpace() const;
    QColor color(UI::ColorRole role, UI::UIState state) const;
    int fontSize(UI::UIScale scale) const;
    QPixmap icon(UI::IconRole role, UI::UIScale scale, UI::UIState state) const;
    QString iconPath(UI::IconRole role) const;
    int iconSize(UI::UIScale scale) const;
    int gizmoSize(UI::UIScale scale) const;
    QString roleName(UI::ColorRole role) const;
    QString roleName(UI::IconRole role) const;
    QString roleName(UI::UIScale scale) const;

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
        QString styleSheet;
        QHash<QString, QColor> palette;
        QHash<QString, QString> icons;
        QHash<QString, int> fontSizes;
        QHash<QString, int> iconSizes;
        QHash<QString, int> gizmoSizes;
        double transformMoveSnap = 1.0;
        double transformRotateSnap = 1.0;
        double transformScaleSnap = 1.0;
        mutable QHash<IconKey, QPixmap> pixmaps;
        QPointer<QObject> object;
    };

    Data d;
};

inline size_t
qHash(const UIPrivate::IconKey& k, size_t seed = 0)
{
    seed = ::qHash(k.role, seed);
    seed = ::qHash(k.scale, seed);
    seed = ::qHash(k.state, seed);
    seed = ::qHash(k.physicalSize, seed);
    return seed;
}

UIPrivate::UIPrivate() = default;
UIPrivate::~UIPrivate() = default;

void
UIPrivate::init()
{
    QFile file(":/style/resources/App.qss");
    if (file.open(QFile::ReadOnly))
        d.styleSheet = QString::fromUtf8(file.readAll());
    updateTheme();
    updateColorSpace(QColorSpace::SRgb);
    updateStylesheet();
}

void
UIPrivate::updateColorSpace(const QColorSpace& colorSpace)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(colorSpace);
    QSurfaceFormat::setDefaultFormat(format);
}

void
UIPrivate::updateStylesheet()
{
    if (d.styleSheet.isEmpty())
        return;

    const QString styleSheet = d.styleSheet;

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
            UI::ColorRole role = UI::ColorRole::Base;
            UI::UIState state = UI::UIState::Normal;
            {
                const QMetaEnum me = QMetaEnum::fromType<UI::ColorRole>();
                for (int i = 0; i < me.keyCount(); ++i) {
                    if (QString::fromLatin1(me.key(i)).compare(roleStr, Qt::CaseInsensitive) == 0) {
                        role = static_cast<UI::ColorRole>(me.value(i));
                        break;
                    }
                }
            }
            if (!stateStr.isEmpty()) {
                const QMetaEnum me = QMetaEnum::fromType<UI::UIState>();
                for (int i = 0; i < me.keyCount(); ++i) {
                    if (QString::fromLatin1(me.key(i)).compare(stateStr, Qt::CaseInsensitive) == 0) {
                        state = static_cast<UI::UIState>(me.value(i));
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
UIPrivate::updateTheme()
{
    os::setDarkTheme();
    d.palette.clear();

    auto map = [&](UI::ColorRole role, QColor color) { d.palette[roleName(role)] = color; };
    map(UI::ColorRole::Accent, QColor::fromHsl(220, 6, 20));
    map(UI::ColorRole::AccentAlt, QColor::fromHsl(220, 6, 24));
    map(UI::ColorRole::AxisX, QColor::fromHsl(0, 150, 100));
    map(UI::ColorRole::AxisY, QColor::fromHsl(130, 150, 100));
    map(UI::ColorRole::AxisZ, QColor::fromHsl(220, 150, 100));
    map(UI::ColorRole::Base, QColor::fromHsl(220, 6, 56));
    map(UI::ColorRole::BaseAlt, QColor::fromHsl(220, 6, 42));
    map(UI::ColorRole::Border, QColor::fromHsl(220, 3, 32));
    map(UI::ColorRole::BorderAlt, QColor::fromHsl(220, 3, 64));
    map(UI::ColorRole::Button, QColor::fromHsl(220, 6, 36));
    map(UI::ColorRole::ButtonAlt, QColor::fromHsl(220, 6, 64));
    map(UI::ColorRole::Error, QColor::fromHsl(352, 158, 93));
    map(UI::ColorRole::Graph, QColor::fromHsl(0, 0, 40));
    map(UI::ColorRole::Grid, QColor::fromHsl(0, 0, 80));
    map(UI::ColorRole::Guide, QColor::fromHsl(0, 0, 120));
    map(UI::ColorRole::Handle, QColor::fromHsl(0, 0, 150));
    map(UI::ColorRole::Highlight, QColor::fromHsl(216, 82, 80));
    map(UI::ColorRole::HighlightAlt, QColor::fromHsl(216, 60, 60));
    map(UI::ColorRole::Item, QColor::fromHsl(220, 6, 50));
    map(UI::ColorRole::ItemAlt, QColor::fromHsl(220, 6, 42));
    map(UI::ColorRole::Progress, QColor::fromHsl(216, 82, 20));
    map(UI::ColorRole::Readonly, QColor::fromHsl(0, 0, 160));
    map(UI::ColorRole::Render, QColor::fromHsl(0, 0, 40));
    map(UI::ColorRole::RenderAlt, QColor::fromHsl(210, 6, 25));
    map(UI::ColorRole::Selection, QColor::fromHsl(35, 240, 160));
    map(UI::ColorRole::SelectionAlt, QColor::fromHsl(55, 140, 120));
    map(UI::ColorRole::Text, QColor::fromHsl(0, 0, 220));
    map(UI::ColorRole::TextAlt, QColor::fromHsl(0, 0, 150));
    map(UI::ColorRole::Warning, QColor::fromHsl(50, 130, 100));

    d.icons[roleName(UI::IconRole::BranchClosed)] = ":/icons/resources/BranchClosed.png";
    d.icons[roleName(UI::IconRole::BranchOpen)] = ":/icons/resources/BranchOpen.png";
    d.icons[roleName(UI::IconRole::Checked)] = ":/icons/resources/Checked.png";
    d.icons[roleName(UI::IconRole::Clear)] = ":/icons/resources/Clear.png";
    d.icons[roleName(UI::IconRole::Code)] = ":/icons/resources/Code.png";
    d.icons[roleName(UI::IconRole::Collapse)] = ":/icons/resources/Collapse.png";
    d.icons[roleName(UI::IconRole::DefaultPrim)] = ":/icons/resources/DefaultPrim.png";
    d.icons[roleName(UI::IconRole::Down)] = ":/icons/resources/Down.png";
    d.icons[roleName(UI::IconRole::DropDown)] = ":/icons/resources/DropDown.png";
    d.icons[roleName(UI::IconRole::Expand)] = ":/icons/resources/Expand.png";
    d.icons[roleName(UI::IconRole::Export)] = ":/icons/resources/Export.png";
    d.icons[roleName(UI::IconRole::ExportImage)] = ":/icons/resources/ExportImage.png";
    d.icons[roleName(UI::IconRole::Follow)] = ":/icons/resources/Follow.png";
    d.icons[roleName(UI::IconRole::FrameAll)] = ":/icons/resources/FrameAll.png";
    d.icons[roleName(UI::IconRole::Geometry)] = ":/icons/resources/Geometry.png";
    d.icons[roleName(UI::IconRole::Hidden)] = ":/icons/resources/Hidden.png";
    d.icons[roleName(UI::IconRole::Instance)] = ":/icons/resources/Instance.png";
    d.icons[roleName(UI::IconRole::Layer)] = ":/icons/resources/Layer.png";
    d.icons[roleName(UI::IconRole::Left)] = ":/icons/resources/Left.png";
    d.icons[roleName(UI::IconRole::List)] = ":/icons/resources/List.png";
    d.icons[roleName(UI::IconRole::Lock)] = ":/icons/resources/Lock.png";
    d.icons[roleName(UI::IconRole::Material)] = ":/icons/resources/Material.png";
    d.icons[roleName(UI::IconRole::Move)] = ":/icons/resources/Move.png";
    d.icons[roleName(UI::IconRole::Nested)] = ":/icons/resources/Nested.png";
    d.icons[roleName(UI::IconRole::New)] = ":/icons/resources/New.png";
    d.icons[roleName(UI::IconRole::Open)] = ":/icons/resources/Open.png";
    d.icons[roleName(UI::IconRole::Over)] = ":/icons/resources/Over.png";
    d.icons[roleName(UI::IconRole::Override)] = ":/icons/resources/Override.png";
    d.icons[roleName(UI::IconRole::PartiallyChecked)] = ":/icons/resources/PartiallyChecked.png";
    d.icons[roleName(UI::IconRole::Payload)] = ":/icons/resources/Payload.png";
    d.icons[roleName(UI::IconRole::Pick)] = ":/icons/resources/Pick.png";
    d.icons[roleName(UI::IconRole::Prim)] = ":/icons/resources/Prim.png";
    d.icons[roleName(UI::IconRole::Redo)] = ":/icons/resources/Redo.png";
    d.icons[roleName(UI::IconRole::Right)] = ":/icons/resources/Right.png";
    d.icons[roleName(UI::IconRole::Rotate)] = ":/icons/resources/Rotate.png";
    d.icons[roleName(UI::IconRole::Run)] = ":/icons/resources/Run.png";
    d.icons[roleName(UI::IconRole::Scale)] = ":/icons/resources/Scale.png";
    d.icons[roleName(UI::IconRole::Select)] = ":/icons/resources/Select.png";
    d.icons[roleName(UI::IconRole::Shaded)] = ":/icons/resources/Shaded.png";
    d.icons[roleName(UI::IconRole::Transform)] = ":/icons/resources/Transform.png";
    d.icons[roleName(UI::IconRole::Undo)] = ":/icons/resources/Undo.png";
    d.icons[roleName(UI::IconRole::Unlock)] = ":/icons/resources/Unlock.png";
    d.icons[roleName(UI::IconRole::Up)] = ":/icons/resources/Up.png";
    d.icons[roleName(UI::IconRole::Visible)] = ":/icons/resources/Visible.png";
    d.icons[roleName(UI::IconRole::Wireframe)] = ":/icons/resources/Wireframe.png";

#ifdef Q_OS_WIN
    d.fontSizes[roleName(UI::UIScale::Small)] = 11;
    d.fontSizes[roleName(UI::UIScale::Medium)] = 12;
    d.fontSizes[roleName(UI::UIScale::Large)] = 16;
#else
    d.fontSizes[roleName(UI::UIScale::Small)] = 10;
    d.fontSizes[roleName(UI::UIScale::Medium)] = 11;
    d.fontSizes[roleName(UI::UIScale::Large)] = 14;
#endif

    d.iconSizes[roleName(UI::UIScale::Small)] = 16;
    d.iconSizes[roleName(UI::UIScale::Medium)] = 32;
    d.iconSizes[roleName(UI::UIScale::Large)] = 64;

    d.gizmoSizes[roleName(UI::UIScale::Small)] = 32;
    d.gizmoSizes[roleName(UI::UIScale::Medium)] = 64;
    d.gizmoSizes[roleName(UI::UIScale::Large)] = 128;
}

QColorSpace
UIPrivate::colorSpace() const
{
    return QSurfaceFormat::defaultFormat().colorSpace();
}

QColor
UIPrivate::color(UI::ColorRole role, UI::UIState state) const
{
    QColor color = d.palette.value(roleName(role), QColor());
    if (state == UI::UIState::Disabled)
        return color.darker(150);

    return color;
}

int
UIPrivate::fontSize(UI::UIScale scale) const
{
    return d.fontSizes.value(roleName(scale), -1);
}

QPixmap
UIPrivate::icon(UI::IconRole role, UI::UIScale scale, UI::UIState state) const
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
UIPrivate::iconPath(UI::IconRole role) const
{
    return d.icons.value(roleName(role));
}

int
UIPrivate::iconSize(UI::UIScale scale) const
{
    return d.iconSizes.value(roleName(scale), -1);
}

int
UIPrivate::gizmoSize(UI::UIScale scale) const
{
    return d.gizmoSizes.value(roleName(scale), -1);
}

QString
UIPrivate::roleName(UI::ColorRole role) const
{
    const QMetaEnum me = QMetaEnum::fromType<UI::ColorRole>();
    return QString::fromLatin1(me.valueToKey(role)).toLower();
}

QString
UIPrivate::roleName(UI::IconRole role) const
{
    const QMetaEnum me = QMetaEnum::fromType<UI::IconRole>();
    return QString::fromLatin1(me.valueToKey(role)).toLower();
}

QString
UIPrivate::roleName(UI::UIScale scale) const
{
    const QMetaEnum me = QMetaEnum::fromType<UI::UIScale>();
    return QString::fromLatin1(me.valueToKey(scale)).toLower();
}

UI::UI()
    : p(new UIPrivate())
{
    p->d.object = this;
    p->init();
}

UI::~UI() = default;

QColor
UI::color(ColorRole role, UIState state) const
{
    return p->color(role, state);
}

void
UI::setColor(ColorRole role, const QColor& color)
{
    if (!color.isValid())
        return;

    const QString key = p->roleName(role);
    if (p->d.palette.value(key) == color)
        return;

    p->d.palette[key] = color;
    Q_EMIT colorChanged(role);
}

QPixmap
UI::icon(IconRole role, UIScale scale, UIState state) const
{
    return p->icon(role, scale, state);
}

QString
UI::iconPath(IconRole role) const
{
    return p->iconPath(role);
}

void
UI::setIconPath(IconRole role, const QString& path)
{
    const QString key = p->roleName(role);
    if (p->d.icons.value(key) == path)
        return;

    p->d.icons[key] = path;
    p->d.pixmaps.clear();
}

int
UI::fontSize(UIScale scale) const
{
    return p->fontSize(scale);
}

void
UI::setFontSize(UIScale scale, int size)
{
    if (size <= 0)
        return;

    const QString key = p->roleName(scale);
    if (p->d.fontSizes.value(key) == size)
        return;

    p->d.fontSizes[key] = size;
}

int
UI::iconSize(UIScale scale) const
{
    return p->iconSize(scale);
}

void
UI::setIconSize(UIScale scale, int size)
{
    if (size <= 0)
        return;

    const QString key = p->roleName(scale);
    if (p->d.iconSizes.value(key) == size)
        return;

    p->d.iconSizes[key] = size;
    p->d.pixmaps.clear();
}

int
UI::gizmoSize(UIScale scale) const
{
    return p->gizmoSize(scale);
}

void
UI::setGizmoSize(UIScale scale, int size)
{
    if (size <= 0)
        return;

    const QString key = p->roleName(scale);
    if (p->d.gizmoSizes.value(key) == size)
        return;

    p->d.gizmoSizes[key] = size;
}

double
UI::transformMoveSnap() const
{
    return p->d.transformMoveSnap;
}

void
UI::setTransformMoveSnap(double value)
{
    if (std::isfinite(value) && value > 0.0)
        p->d.transformMoveSnap = value;
}

double
UI::transformRotateSnap() const
{
    return p->d.transformRotateSnap;
}

void
UI::setTransformRotateSnap(double value)
{
    if (std::isfinite(value) && value > 0.0)
        p->d.transformRotateSnap = value;
}

double
UI::transformScaleSnap() const
{
    return p->d.transformScaleSnap;
}

void
UI::setTransformScaleSnap(double value)
{
    if (std::isfinite(value) && value > 0.0)
        p->d.transformScaleSnap = value;
}

void
UI::refresh()
{
    p->d.pixmaps.clear();
    p->updateStylesheet();
}

void
UI::setColorSpace(const QColorSpace& colorSpace)
{
    p->updateColorSpace(colorSpace);
}

QColorSpace
UI::colorSpace() const
{
    return p->colorSpace();
}

void
UI::setStyleSheet(const QString& styleSheet)
{
    if (p->d.styleSheet == styleSheet)
        return;

    p->d.styleSheet = styleSheet;
    refresh();
}

QString
UI::styleSheet() const
{
    return p->d.styleSheet;
}

}  // namespace stageviz
