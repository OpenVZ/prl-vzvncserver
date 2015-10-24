DEBUG=1

BUILD_VERSION ?= "\"7.0.0\""
VERSION=$(if $(BUILD_VERSION),-DVER_PRODUCTVERSION_STR=$(BUILD_VERSION))
CFLAGS += $(if $(DEBUG),-g -O0 -DDEBUG,-O2) $(VERSION) -D_LIN_ -Wall -c -I /usr/include/prlsdk/
LDFLAGS += $(if $(DEBUG),-g  -rdynamic,) -lprl_sdk -lpthread -lvncserver -lvzctl2

OBJS = \
	console.o \
	main.o \
	util.o \
	vt100.o

BINARY=prl_vzvncserver_app

all: depend $(BINARY)

$(BINARY): $(OBJS)
	g++ -o $@ $(prlctl_OBJS) $(OBJS) $(LDFLAGS)

%.o: %.c
	g++ -c $(CFLAGS) -o $@ $<

depend:
	g++ $(CFLAGS) -M $(OBJS:.o=.c) > depend

install:
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(BINARY) $(DESTDIR)/usr/bin

clean:
	rm -f *.o $(BINARY) depend

.PHONY: all install clean depend
