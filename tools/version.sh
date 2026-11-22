#!/usr/bin/env bash
#
# dnx RTOS version (file VERSION: MAJOR.MINOR.PATCH).
#
# Usage: tools/version.sh [command]
#   (none)          version of build: VERSION, outside release branch with
#                   branch name (e.g. 3.13.0+develop, 3.13.0+feature-gpt)
#   hash            commit hash of build, "-dirty" when tracked files are
#                   modified (e.g. 5a96260-dirty)
#   check           warning when release branch (release/X.Y or
#                   release/X.Y.Z) does not match VERSION
#   bump <part>     increase part of version (major, minor, patch) and commit
#                   VERSION ("Version X.Y.Z."); NOCOMMIT=1: file only
#   tag             create tag vX.Y.Z on current commit (VERSION)
#
# Branches: feature/* -> develop; release/X.Y from develop after bump of
# minor/major version; release fixes: bump patch on release branch, tag of
# each released commit.

cd "$(dirname "$0")/.."

version() {
    tr -d ' \t\r\n' < VERSION 2>/dev/null
}

branch() {
    git symbolic-ref --short HEAD 2>/dev/null
}

die() {
    echo "version: $*" >&2
    exit 1
}

VER=$(version)
[[ "$VER" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "invalid VERSION file: '$VER'"
MAJOR=${BASH_REMATCH[1]}
MINOR=${BASH_REMATCH[2]}
PATCH=${BASH_REMATCH[3]}

case "$1" in
"")
    b=$(branch)
    if [ -n "$b" ] && [[ "$b" != release/* ]]; then
        echo "$VER+$(echo "$b" | tr -c 'A-Za-z0-9.\n' '-')"
    else
        echo "$VER"
    fi
    ;;

hash)
    h=$(git rev-parse --short HEAD 2>/dev/null)
    if [ -n "$h" ] && [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then
        h="$h-dirty"
    fi
    echo "$h"
    ;;

check)
    b=$(branch)
    if [[ "$b" == release/* ]]; then
        r=${b#release/}
        if [ "$r" != "$VER" ] && [ "$r" != "$MAJOR.$MINOR" ]; then
            echo "WARNING: branch $b does not match VERSION $VER (tools/version.sh bump)" >&2
        fi
    fi
    ;;

bump)
    case "$2" in
    major) VER="$((MAJOR + 1)).0.0" ;;
    minor) VER="$MAJOR.$((MINOR + 1)).0" ;;
    patch) VER="$MAJOR.$MINOR.$((PATCH + 1))" ;;
    *) die "usage: $0 bump major|minor|patch" ;;
    esac

    echo "$VER" > VERSION
    echo "Version: $VER"

    if [ "$NOCOMMIT" != "1" ]; then
        git commit -q -m "Version $VER." -- VERSION || die "commit failed"
        echo "Committed: Version $VER. (tag of release: make tag-release)"
    fi
    ;;

tag)
    git diff --quiet HEAD -- VERSION 2>/dev/null || die "VERSION is not committed"
    git rev-parse -q --verify "refs/tags/v$VER" >/dev/null && die "tag v$VER already exists"
    git tag -a "v$VER" -m "dnx RTOS $VER" || die "tag failed"
    echo "Tag v$VER created (git push origin v$VER)"
    ;;

*)
    die "unknown command: $1 (see $0)"
    ;;
esac
