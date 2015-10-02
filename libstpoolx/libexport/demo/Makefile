#piggy_xrh@163.com

.PHONY:all clean

include env.mk

OBJS_DIR :=.obj

EXAMPLE := demo demo-pri demo-sche demo-timer demo-c++ demo-c++-mpool

all:PREPARE $(EXAMPLE)

PREPARE:   
	@for d in $(OBJS_DIR); do \
		[ -d $$d ] || mkdir -p $$d; \
	done

demos: $(EXAMPLE)
	
demo:$(OBJS_DIR)/demo.o $(addprefix $(LIB_DIR)/, libstpool.a libmsglog.a)	
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo-pri:$(OBJS_DIR)/demo-pri.o $(addprefix $(LIB_DIR)/, libstpool.a libmsglog.a)	
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo-sche:$(OBJS_DIR)/demo-sche.o $(addprefix $(LIB_DIR)/, libstpool.a libmsglog.a)		
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo-timer:$(OBJS_DIR)/demo-timer.o $(addprefix $(LIB_DIR)/, libtevent.a libstpool.a libmsglog.a)		
	$(CC) $(CFLAGS) -o$@ $^ -lpthread -lrt -lm

demo-c++:$(OBJS_DIR)/demo-c++.o $(addprefix $(LIB_DIR)/, libstpool.a libstpoolc++.a libmsglog.a)
	$(CPP) $(CPPFLAGS) -o$@ -Xlinker "-(" $^ -Xlinker "-)" -lpthread -lrt 

demo-c++-mpool:$(OBJS_DIR)/demo-c++-mpool.o $(addprefix $(LIB_DIR)/, libstpool.a libstpoolc++.a libmsglog.a)
	$(CPP) $(CFLAGS) -o$@ -Xlinker "-(" $^ -Xlinker "-)" -lpthread -lrt


$(OBJS_DIR)/%.o:%.c
	$(CC) -c $(CFLAGS) -c $^ -o$@

$(OBJS_DIR)/%.o:%.cpp
	$(CPP) -c $(CFLAGS) -c $^ -o$@

clean:
	-@rm $(OBJS_DIR)/*.o $(EXAMPLE) *.o
