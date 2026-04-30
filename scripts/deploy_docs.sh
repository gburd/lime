#!/usr/bin/env bash
set -euo pipefail

# Generate Doxygen docs and push to the pages branch.
# Works for both Codeberg Pages and GitHub Pages.
#
# Usage: ./scripts/deploy_docs.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Generate docs
echo "Generating Doxygen docs..."
cd docs
doxygen Doxyfile
cd "$ROOT"

if [ ! -d docs/api/html ]; then
  echo "Error: docs/api/html not found. Doxygen failed?" >&2
  exit 1
fi

# Build pages branch content in a temp dir
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cp -r docs/api/html/* "$TMPDIR/"

# Create/update orphan pages branch
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
COMMIT_SHA=$(git rev-parse --short HEAD)

cd "$TMPDIR"
git init
git checkout -b pages
git add -A
git commit -m "docs: generate from ${COMMIT_SHA}"

# Push to origin
REMOTE_URL=$(cd "$ROOT" && git remote get-url origin)
git remote add origin "$REMOTE_URL"
git push -f origin pages

echo "Pushed pages branch to origin."
echo "Codeberg: https://gregburd.codeberg.page/lime/"
echo "GitHub:   configure Settings > Pages > Source: pages branch"
