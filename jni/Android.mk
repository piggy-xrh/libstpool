#piggy_xrh@163.com

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

CFLAGS += -I. -Imsglog -Ios -Ifactory -Ipool -Ipool/core -Ipool/com -Ipool/rt -Ipool/gp

#libmsglog
LOCAL_MODULE := libmsglog
LOCAL_SRC_FILES := ../msglog/msglog.c 
include $(BUILD_STATIC_LIBRARY)


#libstpool
include $(CLEAR_VARS)
LIB_STPOOL_SRC := $(addprefix ../, stpool.c stpool_group.c) 

LIB_STPOOL_SRC += $(addprefix ../factory/, cpool_factory.c) 

LIB_STPOOL_SRC += $(addprefix ../os/, ospx.c ospx_error.c) 

LIB_STPOOL_SRC += $(addprefix ../pool/core/, sm_cache.c objpool.c cpool_core.c) 

LIB_STPOOL_SRC += $(addprefix ../pool/com/, cpool_com_method.c cpool_wait.c) 

LIB_STPOOL_SRC += $(addprefix ../pool/rt/, cpool_rt_factory.c cpool_rt_core_method.c cpool_rt_method.c \
				                          cpool_rt_internal.c cpool_rt_scheduler_dump.c)

LIB_STPOOL_SRC += $(addprefix ../pool/gp/, cpool_gp_factory.c cpool_gp_core_method.c cpool_gp_method.c \
										   cpool_gp_advance_method.c \
			  							   cpool_gp_internal.c cpool_gp_entry.c cpool_gp_wait.c)
LOCAL_MODULE := libstpool
LOCAL_SRC_FILES := $(LIB_STPOOL_SRC)
LOCAL_CFLAGS = $(CFLAGS) 
LOCAL_STATIC_LIBRARIES := libmsglog
include $(BUILD_STATIC_LIBRARY) 


