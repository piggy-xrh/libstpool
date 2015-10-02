CROSS :=
CC    :=$(CROSS)gcc
CPP   :=$(CROSS)g++
AR    :=$(CROSS)ar
STRIP :=$(CROSS)strip

ARFLAGS = -rv
STRIPFLAGS = -xXg
CFLAGS = $(addprefix -I, $(LIB_INCLUDE_DIR))
CPPFLAGS = $(addprefix -I, $(LIB_INCLUDE_DIR))


#---------------------------------------------
LIB_INCLUDE_DIR :=../include

#You should specify the library directory
LIB_DIR := ../lib/Release/x86_32_linux
CFLAGS += -O2
CPPFLAGS += -O2


