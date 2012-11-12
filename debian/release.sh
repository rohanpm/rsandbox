#!/bin/sh
set -x
set -e

PKG=rsandbox

TAG="$1"
if [ "x$TAG" = "x" ]; then
    echo "Usage: release.sh <release-tag> [<count>]" 1>&2
    exit 2
fi

COUNT="$2"

if [ -z "$COUNT" ]; then
    COUNT=1
fi

SHA=$(git rev-parse --verify $TAG)
if [ "x$SHA" = "x" ]; then
    exit 3
fi

for dist in \
    oneiric \
    precise \
    quantal \
    ; do
    git reset --hard $SHA
    sed -r -e "1 s/\((.+)\)/(\1~${dist}${COUNT})/" -i debian/changelog
    dch -a "git revision $SHA"
    dch --release --distribution $dist ""
    rm -f ../${PKG}*${dist}${COUNT}*.dsc ../${PKG}*${dist}${COUNT}*source.changes ../${PKG}*${dist}${COUNT}*.ppa.upload
    dpkg-buildpackage -S -kAD117A2E
    dput ppa:rohanpm/${PKG} ../${PKG}*${dist}${COUNT}*source.changes
done
git reset --hard $SHA
