LOCAL_PATH := $(call my-dir)/$(FREETYPE)

include $(CLEAR_VARS)

LOCAL_MODULE := freetype
LOCAL_ARM_MODE := arm

LOCAL_CFLAGS := -O3 \
  -DFT2_BUILD_LIBRARY -DDARWIN_NO_CARBON -DHAVE_STDINT_H \
  '-DFT_CONFIG_MODULES_H="slimftmodules.h"' \
  '-DFT_CONFIG_OPTIONS_H="slimftoptions.h"'

LOCAL_C_INCLUDES := \
  $(LOCAL_PATH)/include \
  $(LOCAL_PATH)/../$(MUPDF)/scripts

LOCAL_SRC_FILES := \
  src/autofit/autofit.c \
  src/base/ftbase.c \
  src/base/ftbbox.c \
  src/base/ftbitmap.c \
  src/base/ftcid.c \
  src/base/ftdebug.c \
  src/base/ftfstype.c \
  src/base/ftgasp.c \
  src/base/ftglyph.c \
  src/base/ftgxval.c \
  src/base/ftinit.c \
  src/base/ftlcdfil.c \
  src/base/ftmm.c \
  src/base/ftotval.c \
  src/base/ftpatent.c \
  src/base/ftstroke.c \
  src/base/ftsynth.c \
  src/base/ftsystem.c \
  src/base/fttype1.c \
  src/base/ftxf86.c \
  src/cache/ftcache.c \
  src/cff/cff.c \
  src/cid/type1cid.c \
  src/gxvalid/gxvalid.c \
  src/otvalid/otvalid.c \
  src/psaux/psaux.c \
  src/pshinter/pshinter.c \
  src/psnames/psnames.c \
  src/raster/raster.c \
  src/sfnt/sfnt.c \
  src/smooth/smooth.c \
  src/truetype/truetype.c \
  src/type1/type1.c

LOCAL_LDLIBS := -lz

include $(BUILD_STATIC_LIBRARY)
