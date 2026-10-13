#!/usr/bin/env bash
# Installs the APK on the CI emulator, starts it, taps around a little, and saves screenshots and
# logs into ./smoke/ for report-smoke.py to turn into annotations.
set -x
PKG=io.github.weirdwallydev.dq9alchemy
mkdir -p smoke
adb wait-for-device
adb shell settings put global window_animation_scale 0
adb install -r "$1" > smoke/install.txt 2>&1
cat smoke/install.txt
adb logcat -c
adb shell am start -W -n "$PKG/org.qtproject.qt.android.bindings.QtActivity"
size=$(adb shell wm size | sed -nE 's/.*: ([0-9]+)x([0-9]+).*/\1 \2/p' | tail -n1)
w=${size% *}; h=${size#* }
echo "screen ${w}x${h}"
sleep 60                                   # first launch downloads the enemy and weapon lists
adb exec-out screencap -p > smoke/1-start.png
echo "pid after start: $(adb shell pidof $PKG)" | tee smoke/pid.txt
# Pick the category list's first entry (bookmarks are empty on a fresh install, so open the type
# list and choose the second entry, then tap the first item).
adb shell input tap $((w / 2)) $((h * 13 / 100))
sleep 3
adb exec-out screencap -p > smoke/2-category.png
adb shell input keyevent KEYCODE_DPAD_DOWN
adb shell input keyevent KEYCODE_DPAD_DOWN
adb shell input keyevent KEYCODE_ENTER
sleep 3
adb shell input tap $((w / 2)) $((h * 30 / 100))
sleep 10
adb exec-out screencap -p > smoke/3-item.png
adb shell input tap $((w / 10)) $((h * 55 / 100))
sleep 3
adb shell input tap $((w / 2)) $((h * 50 / 100))
sleep 8
adb exec-out screencap -p > smoke/4-tap.png
echo "pid at end: $(adb shell pidof $PKG)" | tee -a smoke/pid.txt
adb logcat -d > smoke/logcat.txt
adb logcat -d -b crash > smoke/crash.txt
true
