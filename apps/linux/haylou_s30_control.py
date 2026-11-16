#!/usr/bin/env python3
"""Compatibility launcher for the native HAYLOU S30 Control application."""

from pathlib import Path
import os
import sys

app_paths = (
    Path(__file__).resolve().parent / "standalone" / "HaylouS30Control",
    Path.home() / ".local" / "opt" / "haylou-s30-control" / "HaylouS30Control",
)
for app_path in app_paths:
    if app_path.is_file() and os.access(app_path, os.X_OK):
        os.execv(str(app_path), [str(app_path), *sys.argv[1:]])

sys.exit("HAYLOU S30 Control is not installed. Run install-user.sh first.")
