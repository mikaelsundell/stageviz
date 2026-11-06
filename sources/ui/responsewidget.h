// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#ifndef STAGEVIZ_RESPONSEWIDGET_H
#define STAGEVIZ_RESPONSEWIDGET_H

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <initializer_list>
#include <utility>
#include <vector>

namespace stageviz {

/**
 * @brief Shows or hides groups of controls according to the available widget width.
 */
class ResponseWidget final : public QObject {
public:
    /**
     * @brief Observes width changes of the supplied widget.
     *
     * @param widget Owns this helper and provides the available width.
     */
    explicit ResponseWidget(QWidget* widget)
        : QObject(widget)
        , m_widget(widget)
    {
        if (m_widget)
            m_widget->installEventFilter(this);
    }

    /**
     * @brief Registers controls to show when the minimum width is available.
     *
     * @param minimumWidth Minimum content width in logical pixels.
     * @param widgets Controls whose visibility is managed together.
     */
    void addGroup(int minimumWidth, std::initializer_list<QWidget*> widgets)
    {
        Group group;
        group.minimumWidth = minimumWidth;
        group.widgets.reserve(widgets.size());
        for (QWidget* widget : widgets)
            group.widgets.emplace_back(widget);
        m_groups.push_back(std::move(group));
        updateVisibility();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_widget && event->type() == QEvent::Resize)
            updateVisibility();
        return QObject::eventFilter(watched, event);
    }

private:
    struct Group {
        int minimumWidth = 0;
        std::vector<QPointer<QWidget>> widgets;
    };

    void updateVisibility()
    {
        if (!m_widget)
            return;
        const int width = m_widget->contentsRect().width();
        for (const Group& group : m_groups) {
            const bool visible = width >= group.minimumWidth;
            for (const QPointer<QWidget>& widget : group.widgets) {
                if (widget && widget->isHidden() != !visible)
                    widget->setVisible(visible);
            }
        }
    }

    QPointer<QWidget> m_widget;
    std::vector<Group> m_groups;
};

}  // namespace stageviz

#endif  // STAGEVIZ_RESPONSEWIDGET_H
