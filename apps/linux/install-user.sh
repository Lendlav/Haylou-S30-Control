#!/bin/sh
set -eu

app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$app_dir/native/build/HaylouS30Control" ]; then
    source_binary="$app_dir/native/build/HaylouS30Control"
else
    source_binary="$app_dir/standalone/HaylouS30Control"
fi
target_dir="$HOME/.local/opt/haylou-s30-control"
applications_dir="$HOME/.local/share/applications"
icons_dir="$HOME/.local/share/icons/hicolor/scalable/apps"

mkdir -p "$target_dir" "$applications_dir" "$icons_dir"
install -m 755 "$source_binary" "$target_dir/HaylouS30Control"
install -m 644 "$app_dir/s30-icon.svg" "$icons_dir/s30.svg"
cat > "$applications_dir/haylou-s30-control.desktop" <<DESKTOP
[Desktop Entry]
Version=1.0
Type=Application
Name=HAYLOU S30
Comment=Управление ANC, эквалайзером и громкостью HAYLOU S30
Exec="$target_dir/HaylouS30Control"
Icon=$icons_dir/s30.svg
Terminal=false
Categories=AudioVideo;Audio;Utility;
StartupNotify=true
StartupWMClass=HaylouS30Control
DESKTOP
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$applications_dir"
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache --force --ignore-theme-index "$HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
fi
printf 'Installed HAYLOU S30 Control to %s\n' "$target_dir"
