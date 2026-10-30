// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "timelineview.h"
#include "application.h"
#include "session.h"
#include "tracelocks.h"
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <limits>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// generated files
#include "ui_timelineview.h"

namespace stageviz {

class TimelineViewPrivate : public QObject {
public:
    TimelineViewPrivate();
    void init();
    void updateStage(UsdStageRefPtr stage, Session::StageStatus status);
    void setFrame(int frame);
    void togglePlayback(bool playing);

    struct Data {
        int minimum = 0;
        int maximum = 0;
        double framesPerSecond = 24.0;
        QTimer timer;
        QScopedPointer<Ui_TimelineView> ui;
        QPointer<TimelineView> view;
    };
    Data d;
};

TimelineViewPrivate::TimelineViewPrivate() = default;

void
TimelineViewPrivate::init()
{
    d.ui.reset(new Ui_TimelineView());
    d.ui->setupUi(d.view.data());
    d.ui->timelineSlider->setTimelineMode(true);
    d.timer.setInterval(1000 / 24);

    connect(session(), &Session::stageChanged, this,
            [this](UsdStageRefPtr stage, Session::LoadPolicy, Session::StageStatus status) {
                updateStage(stage, status);
            });
    connect(d.ui->timelineSlider, &QSlider::valueChanged, this, &TimelineViewPrivate::setFrame);
    connect(d.ui->timelineFrame, &QSpinBox::valueChanged, this, &TimelineViewPrivate::setFrame);
    connect(d.ui->timelinePlay, &QToolButton::toggled, this, &TimelineViewPrivate::togglePlayback);
    connect(&d.timer, &QTimer::timeout, this, [this]() {
        const int nextFrame = d.ui->timelineSlider->value() >= d.maximum ? d.minimum
                                                                         : d.ui->timelineSlider->value() + 1;
        d.ui->timelineSlider->setValue(nextFrame);
    });

    QTimer::singleShot(0, d.view, [this]() {
        const UsdStageRefPtr stage = session()->stage();
        updateStage(stage, stage ? Session::StageStatus::Loaded : Session::StageStatus::Closed);
    });
}

void
TimelineViewPrivate::updateStage(UsdStageRefPtr stage, Session::StageStatus status)
{
    d.timer.stop();
    {
        const QSignalBlocker blocker(d.ui->timelinePlay);
        d.ui->timelinePlay->setChecked(false);
        d.ui->timelinePlay->setText(tr("Play"));
    }

    double start = 0.0;
    double end = 0.0;
    d.framesPerSecond = 24.0;
    bool hasStageRange = false;
    QList<int> keyframes;
    if (status == Session::StageStatus::Loaded && stage) {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        start = stage->GetStartTimeCode();
        end = stage->GetEndTimeCode();
        const double timeCodesPerSecond = stage->GetTimeCodesPerSecond();
        if (std::isfinite(timeCodesPerSecond) && timeCodesPerSecond > 0.0)
            d.framesPerSecond = timeCodesPerSecond;
        hasStageRange = end > start;

        QSet<int> uniqueKeyframes;
        double sampleStart = std::numeric_limits<double>::max();
        double sampleEnd = std::numeric_limits<double>::lowest();
        for (const UsdPrim& prim : UsdPrimRange::Stage(stage)) {
            for (const UsdAttribute& attribute : prim.GetAttributes()) {
                std::vector<double> samples;
                if (!attribute.GetTimeSamples(&samples))
                    continue;
                for (const double sample : samples) {
                    if (!std::isfinite(sample))
                        continue;
                    sampleStart = std::min(sampleStart, sample);
                    sampleEnd = std::max(sampleEnd, sample);
                    const int frame = static_cast<int>(std::clamp(std::round(sample), -1000000.0, 1000000.0));
                    if (!uniqueKeyframes.contains(frame)) {
                        uniqueKeyframes.insert(frame);
                        keyframes.append(frame);
                    }
                }
            }
        }

        std::sort(keyframes.begin(), keyframes.end());
        if (!keyframes.isEmpty()) {
            if (hasStageRange) {
                start = std::min(start, sampleStart);
                end = std::max(end, sampleEnd);
            }
            else {
                start = sampleStart;
                end = sampleEnd;
            }
        }
    }

    d.minimum = static_cast<int>(std::clamp(std::floor(start), -1000000.0, 1000000.0));
    d.maximum = static_cast<int>(std::clamp(std::ceil(end), -1000000.0, 1000000.0));
    if (d.maximum < d.minimum)
        d.maximum = d.minimum;

    d.ui->timelineSlider->setRange(d.minimum, d.maximum);
    QList<int> tickValues = keyframes;
    if (tickValues.isEmpty() && d.maximum > d.minimum) {
        const int step = std::max(1, (d.maximum - d.minimum + 9) / 10);
        for (int frame = d.minimum; frame < d.maximum; frame += step)
            tickValues.append(frame);
        tickValues.append(d.maximum);
    }
    d.ui->timelineSlider->setTickValues(tickValues);
    d.ui->timelineFrame->setRange(d.minimum, d.maximum);
    d.ui->timelineRange->setText(QStringLiteral("%1 – %2").arg(d.minimum).arg(d.maximum));
    const bool hasTimeline = status == Session::StageStatus::Loaded && stage
                             && (d.maximum > d.minimum || !keyframes.isEmpty());
    d.ui->timelinePlay->setEnabled(hasTimeline);
    d.ui->timelineFrame->setEnabled(hasTimeline);
    d.ui->timelineSlider->setEnabled(hasTimeline);
    d.view->setEnabled(true);
    setFrame(d.minimum);
}

void
TimelineViewPrivate::setFrame(int frame)
{
    const int current = std::clamp(frame, d.minimum, d.maximum);
    {
        const QSignalBlocker sliderBlocker(d.ui->timelineSlider);
        const QSignalBlocker frameBlocker(d.ui->timelineFrame);
        d.ui->timelineSlider->setValue(current);
        d.ui->timelineFrame->setValue(current);
    }
    Q_EMIT d.view->timeCodeChanged(current);
}

void
TimelineViewPrivate::togglePlayback(bool playing)
{
    if (playing && d.maximum > d.minimum) {
        d.ui->timelinePlay->setText(tr("Pause"));
        d.timer.setInterval(std::max(1, static_cast<int>(std::round(1000.0 / d.framesPerSecond))));
        d.timer.start();
    }
    else {
        d.timer.stop();
        d.ui->timelinePlay->setChecked(false);
        d.ui->timelinePlay->setText(tr("Play"));
    }
}

TimelineView::TimelineView(QWidget* parent)
    : QWidget(parent)
    , p(new TimelineViewPrivate())
{
    setAttribute(Qt::WA_StyledBackground, true);
    p->d.view = this;
    p->init();
}

TimelineView::~TimelineView() = default;

}  // namespace stageviz
