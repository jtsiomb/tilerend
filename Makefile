src = $(wildcard src/*.c)
obj = $(src:.c=.o)
dep = $(src:.c=.d)
bin = tilerend

opt = -O3 -ffast-math -fno-strict-aliasing
dbg = -g
def = -DUSE_THREADS

CFLAGS = -pedantic -Wall $(dbg) $(opt) $(def) -pthread -MMD
LDFLAGS = -pthread -lmeshfile -limago -lm

$(bin): $(obj)
	$(CC) -o $@ $(obj) $(LDFLAGS)

-include $(dep)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)
