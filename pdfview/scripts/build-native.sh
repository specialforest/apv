#!/bin/sh
# make sure ndk-build is in path

SCRIPTDIR=`dirname $0`
MUPDF_FILE=mupdf-1.1-source.tar.gz
MUPDF=mupdf-1.1-source
MUPDF_THIRDPARTY_FILE=mupdf-thirdparty-2012-08-14.zip
MUPDF_THIRDPARTY=thirdparty

cd $SCRIPTDIR/../deps
tar xvf $MUPDF_FILE
unzip -o $MUPDF_THIRDPARTY_FILE
cp -r $MUPDF ../jni/
cp -r $MUPDF_THIRDPARTY/* ../jni/

gcc -o ../scripts/fontdump $MUPDF/scripts/fontdump.c
cd ../jni/$MUPDF
mkdir generated 2> /dev/null
../../scripts/fontdump generated/font_base14.h fonts/*.cff
../../scripts/fontdump generated/font_droid.h fonts/droid/DroidSans.ttf fonts/droid/DroidSansMono.ttf
cd ..
ndk-build
