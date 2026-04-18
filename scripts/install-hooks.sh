#!/usr/bin/env bash
# One-time install: point git at the versioned hooks directory.
#
# Run from anywhere inside the repo:
#   bash scripts/install-hooks.sh
#
# This sets repo-local `core.hooksPath` to scripts/git-hooks/, so all
# hooks under that directory become active. Versioned in the repo, so
# every contributor / clone gets the same set after running this once.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOKS_DIR="$REPO_ROOT/scripts/git-hooks"

if [ ! -d "$HOOKS_DIR" ]; then
    echo "ERROR: $HOOKS_DIR not found" >&2
    exit 1
fi

chmod +x "$HOOKS_DIR"/*

git -C "$REPO_ROOT" config core.hooksPath scripts/git-hooks

echo "Installed git hooks from scripts/git-hooks/"
echo
echo "Active hooks:"
ls -1 "$HOOKS_DIR" | sed 's/^/  - /'
echo
echo "To uninstall: git config --unset core.hooksPath"
