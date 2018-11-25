APP = png2arr
BUILDDIR ?= build
INSTALLDIR ?= /usr/bin
V ?= @

CFILES = $(wildcard *.c)
CFLAGS += -I.

OBJFILES = $(CFILES:%.c=$(BUILDDIR)/%.o)
DEPFILES = $(CFILES:%.c=$(BUILDDIR)/%.d)

CFLAGS += \
-Wall -Wno-format-y2k -W -Wstrict-prototypes -Wmissing-prototypes \
-Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch \
-Wshadow -Wcast-align -Wchar-subscripts -Winline -Wnested-externs \
-Wredundant-decls

MKDIR ?= mkdir -p

CFLAGS += $(shell pkg-config libpng --cflags)
LIBS += $(shell pkg-config libpng --libs)

#CFLAGS += -fsanitize=address
#LDFLAGS += -fsanitize=address -fno-omit-frame-pointer -O
#LIBS += -lasan

$(BUILDDIR)/$(APP): $(OBJFILES)
	$(V)@echo "LN\t$@"
	$(V)$(CC) $(LDFLAGS) -o $@ $(OBJFILES) $(LIBS)

$(OBJFILES) : $(BUILDDIR)/%.o:%.c
	$(V)echo "CC\t$@"
	$(V)$(MKDIR) $(@D);
	$(V)$(CC) $(CFLAGS) -g -c -o $@ $<

$(DEPFILES) : $(BUILDDIR)/%.d:%.c
	$(V)rm -f $@; \
	$(MKDIR) $(@D); \
	$(CC) -M $< > $@.$$$$ 2> /dev/null; \
	sed 's,\($*\)\.o[ :]*, $(BUILDDIR)/\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

.PHONY: all clean

clean:
	$(V)echo "CLEAN"
	$(V)rm -rf $(BUILDDIR)
	$(V)rm -f *.gcov

install: $(BUILDDIR)/$(APP)
	$(V)echo "INSTALL"
	$(V)cp $(BUILDDIR)/$(APP) $(INSTALLDIR)/$(APP)

uninstall:
	$(V)echo "UNINSTALL"
	$(V)rm -f $(INSTALLDIR)/$(APP)
