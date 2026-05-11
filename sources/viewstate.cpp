// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewstate.h"
#include "viewcamera.h"

namespace stageviz {
class ViewStatePrivate {
public:
    void init();
    struct Data {
        QScopedPointer<ViewCamera> viewCamera;
    };
    Data d;
};

void
ViewStatePrivate::init()
{
    d.viewCamera.reset(new ViewCamera());
}

ViewState::ViewState(QObject* parent)
    : QObject(parent)
    , p(new ViewStatePrivate())
{
    p->init();
}

ViewState::~ViewState() {}

ViewCamera*
ViewState::camera() const
{
    return p->d.viewCamera.data();
}


}  // namespace stageviz
