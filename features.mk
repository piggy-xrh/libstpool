# LIB mode type (Debug/Release)
LIB_MODE = Release

# OS width (32/64)
OS_WIDTH = 32

# Strip the library (yes/no)
STRIP_LIB = no

# Use statics report for pool (yes/no)
USE_STATICS_REPORT = yes

#libtevent CONFIG_POOL_TIMER option (yes/no)
LIBTEVENT_CONFIG_POOL_TIMER = yes

#------------------------------------------
ifeq ($(LIB_MODE), Debug)
FEATURES_FLAGS  =-g -D_DEBUG -D_GNU_SOURCE -fPIC 
STRIP_LIB = no 
else
FEATURES_FLAGS  =-s -O2 -DNDEBUG -D_GNU_SOURCE -fPIC 
endif

ifeq ($(OS_WIDTH), 64)
FEATURES_FLAGS += -D_OS_WIDTH_TYPE_64 -m64
else
FEATURES_FLAGS += -D_OS_WIDTH_TYPE_32 -m32
endif

ifeq ($(USE_STATICS_REPORT), yes)
FEATURES_FLAGS += -DCONFIG_STATICS_REPORT
endif

ifeq ($(LIBTEVENT_CONFIG_POOL_TIMER), yes)
FEATURES_FLAGS += -DCONFIG_POOL_TIMER
endif
#------------------------------------------


CFLAGS += $(FEATURES_FLAGS) 
CPPFLAGS += $(FEATURES_FLAGS) 

