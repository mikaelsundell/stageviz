// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "tabwidget.h"
#include "application.h"
#include "style.h"
#include <QPainter>
#include <QPointer>
#include <QStyle>

namespace stageviz {

TabWidget::TabBar::TabBar(QWidget* parent)
    : QTabBar(parent)
{}

TabWidget::TabBar::~TabBar() = default;

class TabWidgetPrivate : public QObject {
    Q_OBJECT
public:
    TabWidgetPrivate();
    void init();
    QRect paneRect() const;
    QRect selectedTabRect() const;
    void drawPane(QPainter* painter) const;

public:
    struct Data {
        QPointer<TabWidget::TabBar> tabBar;
        QPointer<TabWidget> tabs;
    };
    Data d;
};

TabWidgetPrivate::TabWidgetPrivate() {}

void
TabWidgetPrivate::init()
{
    d.tabBar = static_cast<TabWidget::TabBar*>(d.tabs->tabBar());
}

QRect
TabWidgetPrivate::paneRect() const
{
    if (!d.tabs)
        return {};

    const QStyleOptionTabWidgetFrame option = d.tabs->tabWidgetFrameOption();
    QRect rect = d.tabs->style()->subElementRect(QStyle::SE_TabWidgetTabPane, &option, d.tabs.data());
    if (rect.isValid())
        rect.adjust(0, 0, -1, -1);

    return rect;
}

QRect
TabWidgetPrivate::selectedTabRect() const
{
    if (!d.tabs || !d.tabBar)
        return {};

    const int index = d.tabs->currentIndex();
    if (index < 0 || index >= d.tabBar->count())
        return {};

    const QRect rect = d.tabBar->tabRect(index);
    const QPoint topLeft = d.tabBar->mapTo(d.tabs.data(), rect.topLeft());

    return QRect(topLeft, rect.size());
}

void
TabWidgetPrivate::drawPane(QPainter* painter) const
{
    if (!painter || !d.tabs)
        return;

    const QRect pane = paneRect();
    if (!pane.isValid())
        return;

    const QColor background = app()->style()->color(Style::ColorRole::Item);
    const QColor border = app()->style()->color(Style::ColorRole::BorderAlt);

    painter->fillRect(pane, background);
    painter->setPen(border);
    painter->drawLine(pane.left(), pane.top(), pane.left(), pane.bottom());
    painter->drawLine(pane.right(), pane.top(), pane.right(), pane.bottom());
    painter->drawLine(pane.left(), pane.bottom(), pane.right(), pane.bottom());

    const QRect selected = selectedTabRect();
    const int y = pane.top();

    if (!selected.isValid()) {
        painter->drawLine(pane.left(), y, pane.right(), y);
        return;
    }
    if (selected.left() > pane.left()) {
        painter->drawLine(pane.left(), y, qMin(selected.left() - 1, pane.right()), y);
    }
    if (selected.right() < pane.right()) {
        painter->drawLine(qMax(selected.right() + 1, pane.left()), y, pane.right(), y);
    }
}

TabWidget::TabWidget(QWidget* parent)
    : QTabWidget(parent)
    , p(new TabWidgetPrivate())
{
    setTabBar(new TabBar(this));
    p->d.tabs = this;
    p->init();
}

TabWidget::~TabWidget() = default;

QStyleOptionTabWidgetFrame
TabWidget::tabWidgetFrameOption() const
{
    QStyleOptionTabWidgetFrame option;
    initStyleOption(&option);
    return option;
}

void
TabWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    p->drawPane(&painter);
}

}  // namespace stageviz

#include "tabwidget.moc"
