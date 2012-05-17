#!/bin/sh

set -e
set -x

if [ -z "$1" -o -z "$2" -o -z "$3" ]; then
  echo "usage: `basename $0` IMAGENAME VOLNAME DIRECTORY"
  exit 1
fi

# invoke this script like: create_dmg image_name "Volume Name"
# it will produce image_name.dmg

mkdir -p build

# if a previous copy of the image exists, remove it
rm -f build/$1.dmg

hdiutil create build/$1.dmg -size 09m -fs HFS+ -volname "$2"

# mount the image and store the device name into dev_handle
dev_handle=`hdid build/$1.dmg | grep Apple_HFS | perl -e '\$_=<>; /^\\/dev\\/(disk.)/; print \$1'`

# copy the software onto the disk
ditto -rsrcFork "$3" "/Volumes/$2/$3"

# unmount the volume
hdiutil detach $dev_handle

# compress the image
hdiutil convert build/$1.dmg -format UDZO -o build/$1.udzo.dmg

# remove the uncompressed image
rm -f build/$1.dmg

# move the compressed image to take its place
mv build/$1.udzo.dmg $1.dmg

hdiutil internet-enable -yes $1.dmg
