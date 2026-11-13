#!/bin/bash
# Launch a ROM in RMG (modern Mupen64Plus front end). Default: the Zapper ROM.
# KWin's compositor stops the emulator's picture from reaching the window on this
# machine, so compositing is switched off while playing and back on afterwards.
cd "$(dirname "$0")"
rom="${1:-Super Mario 64 (Zapper).z64}"

kwin_compositing() { qdbus org.kde.KWin /Compositor "org.kde.kwin.Compositing.$1" >/dev/null 2>&1; }
kwin_compositing suspend
trap 'kwin_compositing resume' EXIT INT TERM

flatpak run com.github.Rosalie241.RMG "$(realpath "$rom")"
