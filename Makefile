#piggy_xrh@163.com

.PHONY:LIBS clean

include complier.mk features.mk

INCLUDE =-I. -Isrc -Ic++
CFLAGS += $(INCLUDE)
CPPFLAGS += $(INCLUDE)

VPATH =.:src:c++:examples
OBJS_DIR :=.obj

DEF_TARGET  :=libmsglog.a libmsglog.so \
			libstpool.a libstpool.so  \
			libstpoolc++.a libstpoolc++.so \
			libtevent.a libtevent.so 

OBJS_tpool :=stpool.o tpool.o ospx.o  ospx_error.o sm_cache.o objpool.o
OBJS_tpool_c++ :=CTaskPool.o CMPool.o CMAllocator.o

OBJS_tevent := tevent.o
OBJS_msglog := msglog.o
LIBS:PREPARE $(DEF_TARGET)

PREPARE:   
	@for d in $(OBJS_DIR) $(OBJS_DEMO_DIR); do \
		[ -d $$d ] || mkdir -p $$d; \
	done

libmsglog.a:$(addprefix $(OBJS_DIR)/, $(OBJS_msglog)) 
	$(AR) $(ARFLAGS) $@ $^ 
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libmsglog.so:$(addprefix $(OBJS_DIR)/, $(OBJS_msglog)) 
	$(CC) --shared -o$@ $^
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libstpool.a:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool)) libmsglog.a
	$(AR) $(ARFLAGS) $@ $^ 
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libstpool.so:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool)) libmsglog.so
	$(CC) --shared -o$@ $^
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libstpoolc++.a:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool_c++)) libmsglog.a
	$(AR) $(ARFLAGS) $@ $^ 
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libstpoolc++.so:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool_c++)) libmsglog.so
	$(CPP) --shared -o$@ $^
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libtevent.a:$(addprefix $(OBJS_DIR)/, $(OBJS_tevent)) libmsglog.a
	$(AR) $(ARFLAGS) $@ $^ 
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libtevent.so:$(addprefix $(OBJS_DIR)/, $(OBJS_tevent)) libmsglog.so
	$(CPP) --shared -o$@ $^
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

EXAMPLE := $(addprefix examples/, demo demo-pri demo-sche demo-c++ demo-c++-mpool demo-timer)
demos: $(EXAMPLE)
all: LIBS demos

%demo:$(OBJS_DIR)/demo.o libstpool.a libmsglog.a
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

%demo-pri:$(OBJS_DIR)/demo-pri.o libstpool.a libmsglog.a
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

%demo-sche:$(OBJS_DIR)/demo-sche.o libstpool.a libmsglog.a
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

%demo-c++:$(OBJS_DIR)/demo-c++.o libstpoolc++.a libstpool.a  libmsglog.a
	$(CPP) $(CPPFLAGS) -o$@ -Xlinker "-(" $^ -Xlinker "-)" -lpthread -lrt 

%demo-c++-mpool:$(OBJS_DIR)/demo-c++-mpool.o libstpoolc++.a libstpool.a libmsglog.a
	$(CPP) $(CPPFLAGS) -o$@ -Xlinker "-(" $^ -Xlinker "-)" -lpthread -lrt

%demo-timer:$(OBJS_DIR)/demo-timer.o  libtevent.a libstpool.a libmsglog.a
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm


$(OBJS_DIR)/%.o:%.c
	$(CC) -c $(CFLAGS) -c $^ -o$@

$(OBJS_DIR)/%.o:%.cpp
	$(CPP) -c $(CPPFLAGS) -c $^ -o$@

demos-clean:
	-@rm $(EXAMPLE)

clean:
	-@rm $(OBJS_DIR)/*.o $(DEF_TARGET) $(EXAMPLE) *.o
