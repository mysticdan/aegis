#!/usr/bin/env bash
set -euo pipefail

# Uninstall AEGIS from ~/.aegis/

INSTALL_DIR="$HOME/.aegis"

if [ -d "$INSTALL_DIR" ]; then
    echo "Removing $INSTALL_DIR..."
    rm -rf "$INSTALL_DIR"
    echo "AEGIS uninstalled."
else
    echo "Nothing to uninstall ($INSTALL_DIR does not exist)."
fi
