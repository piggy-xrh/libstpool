.PHONY:all LIBS clean distclean install uninstall

include complier.mk features.mk

INCLUDE =-I. -Imsglog -Ios -Ifactory -Ipool -Ipool/core -Ipool/com -Ipool/gp -Ipool/rt
CFLAGS += $(INCLUDE)
CPPFLAGS += $(CFLAGS)

VPATH =.:msglog:os:factory:pool:pool/core:pool/com:pool/gp:pool/rt:examples
OBJS_DIR :=.obj

ALL_LIBS :=libmsglog.a libmsglog.so libstpool.a libstpool.so  

OBJS_stpool := ospx.o  ospx_error.o \
			   timer.o sm_cache.o objpool.o \
			   cpool_core.o cpool_core_gc.o \
			   cpool_factory.o cpool_wait.o cpool_com_method.o \
			   cpool_rt_factory.o cpool_rt_core_method.o cpool_rt_method.o  cpool_rt_internal.o\
			   cpool_rt_scheduler_dump.o \
			   cpool_gp_factory.o cpool_gp_core_method.o cpool_gp_method.o cpool_gp_advance_method.o\
			   cpool_gp_internal.o cpool_gp_entry.o cpool_gp_wait.o \
			   stpool.o stpool_group.o

OBJS_msglog := msglog.o

all: LIBS demos
LIBS:PREPARE $(ALL_LIBS)

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
	$(CC) --shared -o  $@ $^
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libstpool.a:$(addprefix $(OBJS_DIR)/, $(OBJS_stpool)) libmsglog.a
	$(AR) $(ARFLAGS) $@ $(filter-out ligmsglog.a, $^)
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

libstpool.so:$(addprefix $(OBJS_DIR)/, $(OBJS_stpool)) libmsglog.so
	$(CC) --shared -o  $@ $^ 
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@
endif

EXAMPLE := $(addprefix examples/, demo demo-pri demo-sche demo-group)

demos: $(EXAMPLE)

LDFLAGS +=-lpthread -lm
ifneq ($(filter -DHAS_CLOCK_GETTIME, $(CFLAGS)), )
LDFLAGS += -lrt
endif

%demo:$(OBJS_DIR)/demo.o libstpool.a libmsglog.a
	$(CC) $^ $(LDFLAGS) -o $@ 

%demo-pri:$(OBJS_DIR)/demo-pri.o libstpool.a libmsglog.a
	$(CC) $^ $(LDFLAGS) -o $@ 

%demo-sche:$(OBJS_DIR)/demo-sche.o libstpool.a libmsglog.a
	$(CC) $^ $(LDFLAGS) -o $@ 

%demo-group:$(OBJS_DIR)/demo-group.o libstpool.a libmsglog.a
	$(CC) $^ $(LDFLAGS) -o $@ 


$(OBJS_DIR)/%.o:%.c
	$(CC) -c $(CFLAGS) -c $^ -o $@

$(OBJS_DIR)/%.o:%.cpp
	$(CPP) -c $(CPPFLAGS) -c $^ -o $@

demos-clean:
	-@rm $(EXAMPLE)

clean:
	-@rm $(OBJS_DIR)/*.o $(ALL_LIBS) $(EXAMPLE) *.o -fr

distclean: clean
	-@rm features.mk complier.mk Makefile

install:LIBS
	@if [ ! -z $(INSTALL_DIR) ]; then \
		if [ ! -d $(INSTALL_DIR)/include/stpool ]; then mkdir -p $(INSTALL_DIR)/include/stpool || exit 1; fi; \
		if [ ! -d $(INSTALL_DIR)/lib ]; then mkdir -p $(INSTALL_DIR)/lib || exit 1; fi; \
		echo "cp stpool.h stpool_group.h stpool_caps.h msglog/msglog.h $(INSTALL_DIR)/include/stpool"; \
		cp stpool.h stpool_group.h stpool_caps.h msglog/msglog.h $(INSTALL_DIR)/include/stpool; \
		echo "cp libmsglog.a libmsglog.so libstpool.a libstpool.so $(INSTALL_DIR)/lib"; \
		cp libmsglog.a libmsglog.so libstpool.a libstpool.so $(INSTALL_DIR)/lib; \
	fi;

uninstall:
	@if [ -d $(INSTALL_DIR) ]; then \
		if [ -d $(INSTALL_DIR)/include ]; then \
			echo "cd $(INSTALL_DIR)/include && rm stpool.h stpool_group.h stpool_caps.h msglog.h"; \
			cd $(INSTALL_DIR)/include && rm -fr stpool.h stpool_group.h stpool_caps.h msglog.h 2>/dev/null; \
			cd .. && rm -d include; \
			cd ..; \
		fi; \
		if [ -d $(INSTALL_DIR)/lib ]; then \
			echo "cd $(INSTALL_DIR)/lib && rm libmsglog.a libmsglog.so libstpool.a libstpool.so"; \
			cd $(INSTALL_DIR)/lib && `rm -fr libmsglog.a libmsglog.so libstpool.a libstpool.so 2>/dev/null`; \
			cd .. && rm -d lib && cd ..; \
		fi;\
		rm -d $(INSTALL_DIR) 2>/dev/null; \
	fi;
