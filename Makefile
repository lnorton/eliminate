
CFLAGS=$(shell gdal-config --cflags) $(shell geos-config --cflags) -O2
LDFLAGS=$(shell gdal-config --libs) $(shell geos-config --clibs)

EXPLODE_OBJECTS=explode_bin.o explode_lib.o commonutils.o
ELIMINATE_OBJECTS=eliminate_bin.o eliminate_lib.o explode_lib.o commonutils.o

all: explode eliminate

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $<

explode: $(EXPLODE_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

eliminate: $(ELIMINATE_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f explode eliminate *.o
