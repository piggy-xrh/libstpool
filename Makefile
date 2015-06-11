#piggy_xrh@163.com

.PHONY:all clean

CROSS :=
CC    :=$(CROSS)gcc
AR    :=$(CROSS)ar
STRIP :=$(CROSS)strip

TARGET  :=libstpool.a libstpool.so  demo demo_pri demo_sche demo_order_task test
OBJS_tpool :=stpool.o tpool.o ospx.o  ospx_error.o mpool.o
OBJS_DIR :=.obj
VPATH =.:src

#Thanks for @pengjiasi: 
#       The GCC option -fPIC should be set at the compiling step.

#CFLAGS  =-Isrc -s -O2 -DNDEBUG -D_GNU_SOURCE -fPIC 

CFLAGS  =-Isrc -g -D_GNU_SOURCE -fPIC 

ARFLAGS = -rv
STRIPFLAGS = -xXg


all:PREPARE $(TARGET)

PREPARE:   
	@for d in $(OBJS_DIR); do \
		[ -d $$d ] || mkdir -p $$d; \
	done

libstpool.a:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool)) 
	$(AR) $(ARFLAGS) $@ $^ 
	$(STRIP) $(STRIPFLAGS) $@
	chmod +x $@

libstpool.so:$(addprefix $(OBJS_DIR)/, $(OBJS_tpool)) 
	$(CC) --shared -o$@ $^
	$(STRIP) $(STRIPFLAGS) $@

demo:demo.o libstpool.a 
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo_pri:demo_pri.o libstpool.a 
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo_sche:demo_sche.o libstpool.a 
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo_order_task:demo_order_task.o libstpool.a 
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

test:test.o libstpool.a 
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm


$(OBJS_DIR)/%.o:%.c
	$(CC) -c $(CFLAGS) -c $^ -o$@

clean:
	-@rm $(OBJS_DIR)/*.o $(TARGET) $(LIB) *.o
