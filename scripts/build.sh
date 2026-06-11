#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# AEGIS Build & Install Script
#
# Builds AEGIS from source and installs the binary and resources to ~/.aegis/.
# After installation, add the following to your shell profile (.bashrc, .zshrc):
#
#   export PATH="$HOME/.aegis/bin:$PATH"
#
# Usage:
#   ./scripts/build.sh                  # Release build
#   ./scripts/build.sh --debug          # Debug build
#   ./scripts/build.sh --sanitize       # Debug + ASan/UBSan
#   ./scripts/build.sh --uninstall      # Remove installation
#   ./scripts/build.sh --help           # Show this help
# =============================================================================

BUILD_TYPE="Release"
DO_INSTALL=true
DO_SANITIZE=false
SHOW_HELP=false

for arg in "$@"; do
    case "$arg" in
        --debug)      BUILD_TYPE="Debug" ;;
        --sanitize)   BUILD_TYPE="Debug"; DO_SANITIZE=true ;;
        --uninstall)  DO_INSTALL=false ;;
        --help|-h)    SHOW_HELP=true ;;
        *)
            echo "Unknown option: $arg" >&2
            echo "Usage: $0 [--debug|--sanitize|--uninstall|--help]" >&2
            exit 1
            ;;
    esac
done

if [ "$SHOW_HELP" = true ]; then
    sed -n '2,/^# =====/p' "$0" | sed 's/^# //' | sed 's/^#//'
    exit 0
fi

# Resolve paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

# Uninstall
if [ "$DO_INSTALL" = false ]; then
    INSTALL_DIR="$HOME/.aegis"
    if [ -d "$INSTALL_DIR" ]; then
        echo "Removing $INSTALL_DIR..."
        rm -rf "$INSTALL_DIR"
        echo "AEGIS uninstalled."
    else
        echo "Nothing to uninstall."
    fi
    exit 0
fi

# Build
BUILD_DIR="$ROOT_DIR/build"
if [ "$DO_SANITIZE" = true ]; then
    BUILD_DIR="$ROOT_DIR/build-sanitize"
fi

echo "=== AEGIS Build ==="
echo "Type:    $BUILD_TYPE"
echo "Sanitize: $DO_SANITIZE"
echo "Source:  $ROOT_DIR"
echo "Build:   $BUILD_DIR"
echo ""

CMAKE_ARGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
)

if [ "$DO_SANITIZE" = true ]; then
    CMAKE_ARGS+=(-DAEGIS_ENABLE_SANITIZERS=ON)
fi

echo "Configuring..."
cmake "${CMAKE_ARGS[@]}"

echo ""
echo "Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Install
INSTALL_DIR="$HOME/.aegis"
BIN_DIR="$INSTALL_DIR/bin"
SHARE_DIR="$INSTALL_DIR/share/aegis"

echo ""
echo "Installing to $INSTALL_DIR..."
mkdir -p "$BIN_DIR" "$SHARE_DIR"
cp "$BUILD_DIR/aegis" "$BIN_DIR/aegis"
cp -r "$ROOT_DIR/config" "$SHARE_DIR/config"
cp -r "$ROOT_DIR/profiles" "$SHARE_DIR/profiles"
cp -r "$ROOT_DIR/prompts" "$SHARE_DIR/prompts"
chmod +x "$BIN_DIR/aegis"

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Binary:  $BIN_DIR/aegis"
echo "Resources: $SHARE_DIR/"
echo ""
echo "Add to your shell profile (~/.bashrc, ~/.zshrc, etc.):"
echo ""
echo "  export PATH=\"\$HOME/.aegis/bin:\$PATH\""
echo ""
echo "Then reload your shell or run:"
echo "  source ~/.bashrc"
echo ""
echo "Verify installation:"
echo "  aegis --version"
echo "  aegis doctor"
