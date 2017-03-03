#
# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH:= $(call my-dir)

exfat_common_cflags := -DHAVE_CONFIG_H -std=gnu99

########################################
# static library: libexfat.a

libexfat_src_files := \
    libexfat/cluster.c \
    libexfat/io.c \
    libexfat/log.c \
    libexfat/lookup.c \
    libexfat/mount.c \
    libexfat/node.c \
    libexfat/time.c \
    libexfat/utf.c \
    libexfat/utils.c

libexfat_headers := \
    $(LOCAL_PATH)/android \
    $(LOCAL_PATH)/libexfat

## TARGET ##
include $(CLEAR_VARS)

LOCAL_MODULE := libexfat
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(libexfat_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(libexfat_headers)

include $(BUILD_STATIC_LIBRARY)

## HOST ##
include $(CLEAR_VARS)

LOCAL_MODULE := libexfat
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(libexfat_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(libexfat_headers)

include $(BUILD_HOST_STATIC_LIBRARY)


########################################
# executable: mkexfatfs

mkexfatfs_src_files := \
    mkfs/cbm.c \
    mkfs/fat.c \
    mkfs/main.c \
    mkfs/mkexfat.c \
    mkfs/rootdir.c \
    mkfs/uct.c \
    mkfs/uctc.c \
    mkfs/vbr.c

mkexfatfs_headers := \
    $(libexfat_headers) \
    $(LOCAL_PATH)/mkfs

## TARGET ##
include $(CLEAR_VARS)

LOCAL_MODULE := mkexfatfs
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(mkexfatfs_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(mkexfatfs_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_EXECUTABLE)

## HOST ##
include $(CLEAR_VARS)

LOCAL_MODULE := mkexfatfs
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(mkexfatfs_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(mkexfatfs_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_HOST_EXECUTABLE)

########################################
# executable: exfatfsck

exfatfsck_src_files := fsck/main.c

exfatfsck_headers := \
    $(libexfat_headers) \
    $(LOCAL_PATH)/fsck

## TARGET ##
include $(CLEAR_VARS)

LOCAL_MODULE := exfatfsck
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(exfatfsck_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(exfatfsck_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_EXECUTABLE)

## HOST ##
include $(CLEAR_VARS)

LOCAL_MODULE := exfatfsck
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(exfatfsck_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(exfatfsck_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_HOST_EXECUTABLE)

########################################
# executable: dumpexfat

dumpexfat_src_files := dump/main.c

dumpexfat_headers := \
    $(libexfat_headers) \
    $(LOCAL_PATH)/dump

## TARGET ##
include $(CLEAR_VARS)

LOCAL_MODULE := dumpexfat
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(dumpexfat_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(dumpexfat_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_EXECUTABLE)

## HOST ##
include $(CLEAR_VARS)

LOCAL_MODULE := dumpexfat
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(dumpexfat_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(dumpexfat_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_HOST_EXECUTABLE)

########################################
# executable: exfatlabel

exfatlabel_src_files := label/main.c

exfatlabel_headers := \
    $(libexfat_headers) \
    $(LOCAL_PATH)/label

## TARGET ##
include $(CLEAR_VARS)

LOCAL_MODULE := exfatlabel
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(exfatlabel_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(exfatlabel_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_EXECUTABLE)

## HOST ##
include $(CLEAR_VARS)

LOCAL_MODULE := exfatlabel
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(exfatlabel_src_files)
LOCAL_CFLAGS := $(exfat_common_cflags)
LOCAL_C_INCLUDES := $(exfatlabel_headers)
LOCAL_STATIC_LIBRARIES := libexfat

include $(BUILD_HOST_EXECUTABLE)
