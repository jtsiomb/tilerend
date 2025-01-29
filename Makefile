PREFIX = /usr/local

src = $(wildcard src/*.c)
obj = $(src:.c=.o)
dep = $(src:.c=.d)
bin = tilerend

opt = -O3 -ffast-math -fno-strict-aliasing
dbg = -g
def = -DUSE_THREADS
incdir = -Ilibs/cgmath/src -Ilibs/imago/src -Ilibs/meshfile/include
libdir = -Llibs/imago -Llibs/meshfile

CFLAGS = -pedantic -Wall $(dbg) $(opt) $(def) $(incdir) -pthread -MMD
LDFLAGS = $(libdir) -pthread -lmeshfile -limago -lpng -lz -ljpeg -lm

$(bin): $(obj) libs
	$(CC) -o $@ $(obj) $(LDFLAGS)

-include $(dep)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: cleandep
cleandep:
	rm -f $(dep)

.PHONY: install
install: $(bin)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(bin) $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: libs
libs:
	$(MAKE) -C libs

.PHONY: clean-libs
clean-libs:
	$(MAKE) -C libs clean
