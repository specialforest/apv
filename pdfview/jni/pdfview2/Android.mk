LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := pdfview2
LOCAL_ARM_MODE := arm
LOCAL_CFLAGS := -O3

LOCAL_C_INCLUDES := \
  $(LOCAL_PATH)/../$(MUPDF)/fitz \
  $(LOCAL_PATH)/../$(MUPDF)/pdf \
  $(LOCAL_PATH)/../$(FREETYPE)/include

LOCAL_SRC_FILES := pdfview2.c

LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -lz -llog  
LOCAL_STATIC_LIBRARIES := mupdf jpeg jbig2dec openjpeg freetype

include $(BUILD_SHARED_LIBRARY)
