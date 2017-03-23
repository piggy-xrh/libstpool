CROSS :=
CC    :=$(CROSS)gcc
CPP   :=$(CROSS)g++
AR    :=$(CROSS)ar
STRIP :=$(CROSS)strip

ARFLAGS = -rv
STRIPFLAGS = -xX
CFLAGS = -Wall
CPPFLAGS = -Wall
