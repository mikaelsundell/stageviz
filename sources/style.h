// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QColor>
#include <QColorSpace>
#include <QObject>
#include <QScopedPointer>

namespace stageviz {
class StylePrivate;
class Style : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Global theme modes.
     */
    enum Theme { Dark, Light };
    Q_ENUM(Theme)

    /**
     * @brief Semantic color roles.
     */
    enum ColorRole {
        Base,
        BaseAlt,
        Dock,
        DockAlt,
        Accent,
        AccentAlt,
        Text,
        Highlight,
        HighlightAlt,
        Border,
        BorderAlt,
        Handle,
        Progress,
        Button,
        ButtonAlt,
        Render,
        RenderAlt,
        Selection,
        SelectionAlt,
        Warning,
        Error
    };
    Q_ENUM(ColorRole)

    /**
     * @brief Semantic icon roles.
     */
    enum IconRole {
        BranchOpen,
        BranchClosed,
        Clear,
        Code,
        Collapse,
        Expand,
        Export,
        ExportImage,
        FrameAll,
        Follow,
        Hidden,
        Visible,
        Checked,
        Dropdown,
        Left,
        Material,
        Mesh,
        PartiallyChecked,
        Open,
        Payload,
        Prim,
        Redo,
        Right,
        Run,
        Shaded,
        Undo,
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
     * @brief Sets the active theme.
     */
    void setTheme(Theme theme);

    /**
     * @brief Returns the active theme.
     */
    Theme theme() const;

    /**
     * @brief Returns color for a role.
     */
    QColor color(ColorRole role, UIState state = UIState::Normal) const;

    /**
     * @brief Returns icon for a role and size.
     */
    QPixmap icon(IconRole role, UIScale scale = UIScale::Medium, UIState = UIState::Normal) const;

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

Q_SIGNALS:

    /**
     * @brief Emitted when theme changes.
     */
    void themeChanged(Theme theme);

    /**
     * @brief Emitted when a color role changes.
     */
    void colorChanged(ColorRole role);

private:
    Q_DISABLE_COPY_MOVE(Style)
    QScopedPointer<StylePrivate> p;
};

}  // namespace stageviz
