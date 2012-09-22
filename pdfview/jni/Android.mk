LOCAL_PATH := $(call my-dir)
TOP_LOCAL_PATH := $(LOCAL_PATH)

FREETYPE := freetype-2.4.10
JBIG2DEC := jbig2dec
JPEG := jpeg-9
OPENJPEG := openjpeg-1.5.0-patched
MUPDF := mupdf-1.1-source

include $(TOP_LOCAL_PATH)/freetype.mk
include $(TOP_LOCAL_PATH)/jbig2dec.mk
include $(TOP_LOCAL_PATH)/jpeg.mk
include $(TOP_LOCAL_PATH)/mupdf.mk
include $(TOP_LOCAL_PATH)/mupdf_fonts.mk
include $(TOP_LOCAL_PATH)/openjpeg.mk
include $(TOP_LOCAL_PATH)/pdfview2/Android.mk
