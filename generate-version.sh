#!/bin/bash
set -e

REPO_ROOT=$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )
cd $REPO_ROOT

# 'git describe --abbrev=7 --dirty' will output a version in that looks like "v0.1.0-12-g3456789-dirty".
VERSION=$(git describe --abbrev=7 --dirty)

updateVersionFile () {
  if [ "${#VERSION}" -gt 63 ]; then
    echo "The 'git describe --abbrev=7 --dirty' generated version is longer than 63 chars, thus not fitting in the ZenFS superblock. Aborting!" >&2
    return 1
  fi
  printf '#pragma once\n#define ZENFS_VERSION "%s"\n' \
    $VERSION > $REPO_ROOT/fs/version.h
  return 0
}

RET=0
if [ -f "$REPO_ROOT/fs/version.h" ]; then
  PERSISTED_VERSION=$(cat $REPO_ROOT/fs/version.h | grep ' ZENFS_VERSION ' | grep -o '".*"' | sed 's/"//g')

  if [ "$PERSISTED_VERSION" != "$VERSION" ]; then
    RET=$(updateVersionFile)
  fi
else
  RET=$(updateVersionFile)
fi

exit $RET
