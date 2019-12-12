#!/bin/sh

src=`dirname $0`

version=`head -n 1 < $src/debian/changelog | perl -p -e 's/.*?\((.*?)-.*?\).*/$1/'`

echo "Version: $version\n"

rm -f amuencha_$version.orig.tar.gz

cd $src/..

tar cfz amuencha_$version.orig.tar.gz amuencha-$version

cd amuencha-$version

debuild -us -uc
