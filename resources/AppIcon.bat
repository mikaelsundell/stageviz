@echo off
REM Copyright 2022-present Contributors to the stageviz project.
REM SPDX-License-Identifier: BSD-3-Clause
REM https://github.com/mikaelsundell/stageviz

REM Create a temporary folder for resized icons
mkdir AppIcon_ico_set

REM Convert PNG to different sizes and fix sRGB profile issues
oiiotool AppIcon.png --attrib "Exif:ColorSpace" 1 --resize 16x16 -o AppIcon_ico_set\icon_16x16.png
oiiotool AppIcon.png --attrib "Exif:ColorSpace" 1 --resize 32x32 -o AppIcon_ico_set\icon_32x32.png
oiiotool AppIcon.png --attrib "Exif:ColorSpace" 1 --resize 48x48 -o AppIcon_ico_set\icon_48x48.png
oiiotool AppIcon.png --attrib "Exif:ColorSpace" 1 --resize 64x64 -o AppIcon_ico_set\icon_64x64.png
oiiotool AppIcon.png --attrib "Exif:ColorSpace" 1 --resize 128x128 -o AppIcon_ico_set\icon_128x128.png
oiiotool AppIcon.png --attrib "Exif:ColorSpace" 1 --resize 256x256 -o AppIcon_ico_set\icon_256x256.png

REM Create the ICO file using all the resized PNGs
oiiotool ^
    AppIcon_ico_set\icon_16x16.png ^
    AppIcon_ico_set\icon_32x32.png ^
    AppIcon_ico_set\icon_48x48.png ^
    AppIcon_ico_set\icon_64x64.png ^
    AppIcon_ico_set\icon_128x128.png ^
    AppIcon_ico_set\icon_256x256.png ^
    --compression png -o AppIcon.ico
