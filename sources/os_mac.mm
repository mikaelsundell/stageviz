// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "os.h"

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>

#include <os/log.h>
#include <QApplication>
#include <QScreen>

namespace stageviz {
namespace os {
void setDarkTheme()
{
    // we force dark aque no matter appearance set in system settings
    [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
}

QString getApplicationPath()
{
    return QApplication::applicationDirPath() + "/..";
}

QString resolveBookmark(const QString& bookmark)
{
    if (bookmark.isEmpty()) {
        return QString();
    }
    QByteArray bookmarkData = QByteArray::fromBase64(bookmark.toUtf8());
    NSError* error = nil;
    BOOL isstale = NO;
    NSURL *url = [NSURL URLByResolvingBookmarkData:[NSData dataWithBytes:bookmarkData.data() length:bookmarkData.size()]
                                           options:NSURLBookmarkResolutionWithSecurityScope
                                     relativeToURL:nil
                               bookmarkDataIsStale:&isstale
                                             error:&error];
    
    if (url && !error) {
        if ([url startAccessingSecurityScopedResource]) {
            return QString::fromUtf8([[url path] UTF8String]);
        } else {
            QString();
        }
    } else {
        QString();
    }
    return QString();
}

QString saveBookmark(const QString& bookmark)
{
    if (bookmark.isEmpty()) {
        return QString();
    }
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:bookmark.toUtf8().constData()]];
    NSError* error = nil;
    NSData* bookmarkData = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                         includingResourceValuesForKeys:nil
                                          relativeToURL:nil
                                                  error:&error];
    
    if (bookmarkData && !error) {
        QByteArray bookmark((const char *)[bookmarkData bytes], [bookmarkData length]);
        return QString::fromUtf8(bookmark.toBase64());
    } else {
        return QString();
    }
}
}  // namespace os
}  // namespace stageviz
