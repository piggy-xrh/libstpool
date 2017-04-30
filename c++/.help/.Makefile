.PHONY: all clean PREPARE distclean install uninstall

include complier.mk features.mk

Target := libstpoolc++.a libstpoolc++.so

OBJS_dir       := .obj
OBJS_stpoolc++ := stpoolc++.o

all: PREPARE $(Target)

PREPARE:   
	@for d in $(OBJS_dir); do \
		[ -d $$d ] || mkdir -p $$d; \
	done

libstpoolc++.a:$(addprefix $(OBJS_dir)/, $(OBJS_stpoolc++)) 
	$(AR) $(ARFLAGS) $@ $(filter-out ligmsglog.a, $^)
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@ 2>/dev/null
endif

libstpoolc++.so:$(addprefix $(OBJS_dir)/, $(OBJS_stpoolc++)) 
	$(CPP) --shared -o  $@ $^ 
	chmod +x $@
ifeq ($(STRIP_LIB), yes)
	$(STRIP) $(STRIPFLAGS) $@ 2>/dev/null
endif

$(OBJS_dir)/%.o:%.c
	$(CC) -c $(CFLAGS) -c $^ -o $@

$(OBJS_dir)/%.o:%.cpp
	$(CPP) -c $(CFLAGS) -c $^ -o $@

clean:
	-@rm -fr $(OBJS_dir) $(Target) || exit 0 

distclean: clean
	-@rm -f $(ALL_LIBS) features.mk complier.mk Makefile

install: all
	@if [ ! -z $(INSTALL_DIR) ]; then \
		if [ ! -d $(INSTALL_DIR)/include/stpool ]; then mkdir -p $(INSTALL_DIR)/include/stpool || exit 1; fi; \
		if [ ! -d $(INSTALL_DIR)/lib ]; then mkdir -p $(INSTALL_DIR)/lib || exit 1; fi; \
		echo "cp stpoolc++.h $(INSTALL_DIR)/include/stpool"; \
		cp stpoolc++.h $(INSTALL_DIR)/include/stpool; \
		echo "cp libstpoolc++.a libstpoolc++.so $(INSTALL_DIR)/lib"; \
		cp libstpoolc++.a libstpoolc++.so $(INSTALL_DIR)/lib; \
	fi;

uninstall:
	@if [ -d $(INSTALL_DIR) ]; then \
		if [ -d $(INSTALL_DIR)/include/stpool ]; then \
			echo "cd $(INSTALL_DIR)/include/stpool && rm stpool.h stpool_group.h stpool_caps.h msglog.h"; \
			cd $(INSTALL_DIR)/include/stpool; \
			if [ $$? -eq 0 ]; then \
				rm -fr stpoolc++.h  2>/dev/null; \
				cd .. && rm -d stpool 2>/dev/null && cd .. && rm -d include 2>/dev/null; \
			fi\
		fi; \
		if [ -d $(INSTALL_DIR)/lib ]; then \
			echo "cd $(INSTALL_DIR)/lib && rm libstpoolc++.a libstpoolc++.so"; \
			cd $(INSTALL_DIR)/lib; \
			if [ $$? -eq 0 ]; then \
				rm -fr libstpoolc++.a libstpoolc++.so 2>/dev/null; \
				cd .. && rm -d lib 2>/dev/null;\
			fi\
		fi;\
		rm -d $(INSTALL_DIR) 2>/dev/null; \
		exit 0; \
	fi;
