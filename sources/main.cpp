// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "viewer.h"

int
main(int argc, char* argv[])
{
    stageviz::Application app(argc, argv);
    stageviz::Viewer viewer;
    viewer.setArguments(QCoreApplication::arguments());
    viewer.show();
    return app.exec();
}
