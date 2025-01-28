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
meshfile:
	$(MAKE) -C meshfile

.PHONY: clean-meshfile
clean-meshfile:
	$(MAKE) -C meshfile clean


