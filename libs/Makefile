.PHONY: all
all: imago meshfile

.PHONY: clean
clean: clean-imago clean-meshfile

.PHONY: imago
imago: imago/Makefile
	$(MAKE) -C imago

imago/Makefile:
	cd imago && ./configure

.PHONY: clean-imago
clean-imago:
	$(MAKE) -C imago clean

.PHONY: meshfile
meshfile: meshfile/config.mk
	$(MAKE) -C meshfile

meshfile/config.mk:
	cd meshfile && ./configure

.PHONY: clean-meshfile
clean-meshfile:
	$(MAKE) -C meshfile clean


