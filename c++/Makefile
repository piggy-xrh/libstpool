#piggy_xrh@163.com

.PHONY:all clean

#CROSS :=arm-hisiv200-linux-
CC    :=$(CROSS)gcc
CPP   :=$(CROSS)g++
AR    :=$(CROSS)ar
STRIP :=$(CROSS)strip

TARGET  :=libstpoolc++.a libstpoolc++.so  demo 
OBJS_tpool_c   :=stpool.o tpool.o ospx.o  ospx_error.o mpool.o
OBJS_tpool_c++ :=CTaskPool.o CMPool.o CMAllocator.o
OBJS_tpool     := $(OBJS_tpool_c) $(OBJS_tpool_c++)

OBJS_DIR :=.objc++
VPATH =.:../src:..

#Thanks for @pengjiasi: 
#       The GCC option -fPIC should be set at the compiling step.

CFLAGS  =-Isrc -s -O2 -DNDEBUG -D_GNU_SOURCE -fPIC 

#CFLAGS  =-Isrc -g -D_GNU_SOURCE -fPIC

CFLAGS += -D_DISABLE_MPOOL -I. -I../src -I..
ARFLAGS = -rv
STRIPFLAGS = -xXg


all:PREPARE $(TARGET)

PREPARE:   
	@for d in $(OBJS_DIR); do \
		[ -d $$d ] || mkdir -p $$d; \
	done

libstpoolc++.a:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool))
	$(AR) $(ARFLAGS) $@ $^ 
	$(STRIP) $(STRIPFLAGS) $@
	chmod +x $@

libstpoolc++.so:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool)) 
	$(CPP) --shared -o$@ $^
	$(STRIP) $(STRIPFLAGS) $@

demo:$(OBJS_DIR)/democ++.o libstpoolc++.a 
	$(CPP) $(CFLAGS) -o$@ $^ -lpthread -lrt


$(OBJS_DIR)/%.o:%.c
	$(CC) -c $(CFLAGS) -c $^ -o$@

$(OBJS_DIR)/%.o:%.cpp
	$(CPP) -c $(CFLAGS) -c $^ -o$@

clean:
	-@rm $(OBJS_DIR)/*.o $(TARGET) $(LIB) *.o
