ROOT_CFLAGS := $(shell root-config --cflags)
ROOT_LIBS   := $(shell root-config --libs)
FITS_CFLAGS := $(shell pkg-config --cflags cfitsio)
FITS_LIBS   := $(shell pkg-config --libs cfitsio)

CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += $(ROOT_CFLAGS) $(FITS_CFLAGS)
LDLIBS   := $(ROOT_LIBS) $(FITS_LIBS)

BINS := tree2fits rdf2fits test/make_test_tree test/dump_fits

.PHONY: all clean test
all: $(BINS)

tree2fits: tree2fits.cpp convert_lib.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

rdf2fits: rdf2fits.cpp convert_lib.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

test/make_test_tree: test/make_test_tree.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(ROOT_LIBS)

test/dump_fits: test/dump_fits.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(FITS_LIBS)

test: all
	./test/make_test_tree /tmp/test_tree.root
	./tree2fits /tmp/test_tree.root events /tmp/test_tree.fits
	./test/dump_fits /tmp/test_tree.fits
	@echo "=== with a header template (--header + --header-key) ==="
	./tree2fits /tmp/test_tree.root events /tmp/test_tree_hdr.fits \
		--header test/header.txt \
		--header-key "ORIGIN = 'KICP' / written at UChicago"
	./test/dump_fits /tmp/test_tree_hdr.fits
	@echo "=== with separate primary and bintable headers ==="
	./tree2fits /tmp/test_tree.root events /tmp/test_tree_phdr.fits \
		--primary-header test/header.txt \
		--primary-header-key "ORIGIN = 'KICP' / written at UChicago" \
		--header-key "HDUCLAS1 = 'EVENTS' / table contents"
	./test/dump_fits /tmp/test_tree_phdr.fits

clean:
	rm -f $(BINS)
