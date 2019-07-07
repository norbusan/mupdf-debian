MUPDF_PC=$(CURDIR)/../mupdf.pc
LIB_DIR=$(CURDIR)/../tmp/usr/lib
INCLUDE_DIR=$(CURDIR)/../tmp/usr/include

test:
	$(CC) $(CURDIR)/compile-test.c -o /dev/null -I$(INCLUDE_DIR) -L$(LIB_DIR) $(shell pkg-config --cflags --libs --static $(MUPDF_PC))
