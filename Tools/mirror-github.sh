#!/bin/sh
# Push this checkout to the GitHub mirror.
#
# Usage:
#   Tools/mirror-github.sh                 # remote "github", push all branches/tags
#   Tools/mirror-github.sh --mirror        # true mirror push (dedicated repo only)
#   Tools/mirror-github.sh github URL      # explicit remote name and URL
#
# The default SSH URL is git@github.com:vagappc/VagaFileManager.git.
# Authentication must already work (SSH key added to GitHub, or HTTPS PAT).
#
# Never put a token in this script or in the repository.

set -e

cd "$(dirname "$0")/.."

MIRROR=0
if [ "$1" = "--mirror" ]; then
    MIRROR=1
    shift
fi

REMOTE=${1:-github}
URL=${2:-git@github.com:vagappc/VagaFileManager.git}

if git remote get-url "$REMOTE" >/dev/null 2>&1; then
    echo "remote $REMOTE: $(git remote get-url "$REMOTE")"
else
    git remote add "$REMOTE" "$URL"
    echo "added remote $REMOTE -> $URL"
fi

if [ "$MIRROR" = "1" ]; then
    echo "WARNING: git push --mirror deletes remote refs that are not local."
    echo "Use it only on the dedicated GitHub mirror repository."
    git push --mirror "$REMOTE"
else
    git push "$REMOTE" --all
    git push "$REMOTE" --tags
fi

echo "Done: $REMOTE"
