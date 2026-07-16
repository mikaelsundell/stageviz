// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "viewer.h"
#include <QFuture>
#include <QtConcurrent>
#include <pxr/usd/usd/schemaRegistry.h>

int
main(int argc, char* argv[])
{
    QFuture<void> schemaWarmup = QtConcurrent::run([]() { pxr::UsdSchemaRegistry::GetInstance(); });
    stageviz::Application app(argc, argv);
    schemaWarmup.waitForFinished();
    stageviz::Viewer viewer;
    viewer.show();
    return app.exec();
}
