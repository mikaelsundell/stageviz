// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QStyleOptionTabWidgetFrame>
#include <QTabBar>
#include <QTabWidget>
#include <memory>

namespace stageviz {

class TabWidgetPrivate;

/**
 * @class TabWidget
 * @brief QTabWidget with StageViz-specific pane painting.
 *
 * TabWidget keeps the standard QTabWidget API while replacing the pane frame
 * painting with StageViz colors. The top pane border is split around the
 * selected tab so the selected tab visually joins the pane without a line
 * crossing underneath it.
 */
class TabWidget : public QTabWidget {
    Q_OBJECT
public:
    /**
     * @class TabBar
     * @brief Tab bar used by TabWidget.
     *
     * Kept as a nested class so StageViz-specific tab bar behavior can be
     * extended without exposing a separate top-level widget type.
     */
    class TabBar : public QTabBar {
    public:
        /**
         * @brief Creates the tab bar.
         * @param parent Parent widget.
         */
        explicit TabBar(QWidget* parent = nullptr);

        /**
         * @brief Destroys the tab bar.
         */
        ~TabBar() override;
    };

    /**
     * @brief Creates the tab widget.
     * @param parent Parent widget.
     */
    explicit TabWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the tab widget.
     */
    ~TabWidget() override;

    /**
     * @brief Builds the style option used to describe the tab widget frame.
     *
     * This public helper mirrors TreeWidget::itemViewOption() and provides the
     * private implementation with access to QTabWidget::initStyleOption(),
     * which is protected in Qt.
     *
     * @return Initialized tab widget frame style option.
     */
    QStyleOptionTabWidgetFrame tabWidgetFrameOption() const;

protected:
    /**
     * @brief Paints the StageViz pane background and frame.
     * @param event Paint event.
     */
    void paintEvent(QPaintEvent* event) override;

private:
    std::unique_ptr<TabWidgetPrivate> p;
};

}  // namespace stageviz
