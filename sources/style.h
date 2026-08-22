// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QColor>
#include <QColorSpace>
#include <QObject>
#include <QPixmap>
#include <QScopedPointer>
#include <QString>

namespace stageviz {
class StylePrivate;
class Style : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Semantic color roles.
     */
    enum ColorRole {
        Accent,
        AccentAlt,
        AxisX,
        AxisY,
        AxisZ,
        Base,
        BaseAlt,
        Border,
        BorderAlt,
        Button,
        ButtonAlt,
        Error,
        Grid,
        Guide,
        Handle,
        Highlight,
        HighlightAlt,
        Item,
        ItemAlt,
        Progress,
        Render,
        RenderAlt,
        Selection,
        SelectionAlt,
        Text,
        Warning
    };
    Q_ENUM(ColorRole)

    /**
     * @brief Semantic icon roles.
     */
    enum IconRole {
        BranchClosed,
        BranchOpen,
        Checked,
        Clear,
        Code,
        Collapse,
        Down,
        DropDown,
        Expand,
        Export,
        ExportImage,
        Follow,
        FrameAll,
        Geometry,
        Hidden,
        Left,
        Material,
        Open,
        Override,
        PartiallyChecked,
        Payload,
        Prim,
        Redo,
        Right,
        Run,
        Shaded,
        Transform,
        Undo,
        Up,
        Visible,
        Wireframe
    };
    Q_ENUM(IconRole)

    /**
     * @brief Logical UI scale levels.
     */
    enum UIScale { Small, Medium, Large };
    Q_ENUM(UIScale)

    enum UIState { Normal, Disabled };
    Q_ENUM(UIState)

    /**
     * @brief Constructs a Style instance.
     */
    Style();

    /**
     * @brief Destroys the Style instance.
     */
    ~Style() override;

    /** @name Theme */
    ///@{

    /**
     * @brief Returns color for a role.
     */
    QColor color(ColorRole role, UIState state = UIState::Normal) const;

    /**
     * @brief Sets color for a role.
     */
    void setColor(ColorRole role, const QColor& color);

    /**
     * @brief Returns icon for a role and size.
     */
    QPixmap icon(IconRole role, UIScale scale = UIScale::Medium, UIState state = UIState::Normal) const;

    /**
     * @brief Returns icon resource path for a role.
     */
    QString iconPath(IconRole role) const;

    /**
     * @brief Sets icon resource path for a role.
     */
    void setIconPath(IconRole role, const QString& path);

    /**
     * @brief Returns font size for a scale.
     */
    int fontSize(UIScale scale) const;

    /**
     * @brief Sets the font size for a scale.
     */
    void setFontSize(UIScale scale, int size);

    /**
     * @brief Returns icon size for a scale.
     */
    int iconSize(UIScale scale) const;

    /**
     * @brief Sets the icon size for a scale.
     */
    void setIconSize(UIScale scale, int size);

    /**
     * @brief Rebuilds and reapplies the application stylesheet.
     */
    void refresh();

    ///@}

    /** @name Rendering */
    ///@{

    /**
     * @brief Sets rendering color space.
     */
    void setColorSpace(const QColorSpace& colorSpace);

    /**
     * @brief Returns rendering color space.
     */
    QColorSpace colorSpace() const;

    ///@}

    /** @name Stylesheet */
    ///@{

    /**
     * @brief Sets rrendering stylesheet.
     */
    void setStyleSheet(const QString& styleSheet);

    /**
     * @brief Returns rendering stylesheet.
     */
    QString styleSheet() const;

    ///@}

Q_SIGNALS:

    /**
     * @brief Emitted when a color role changes.
     */
    void colorChanged(ColorRole role);

private:
    Q_DISABLE_COPY_MOVE(Style)
    QScopedPointer<StylePrivate> p;
};

}  // namespace stageviz
