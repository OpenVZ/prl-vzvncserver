DEBUG=1

BUILD_VERSION ?= "\"7.0.0\""
VERSION=$(if $(BUILD_VERSION),-DVER_PRODUCTVERSION_STR=$(BUILD_VERSION))
CC = gcc
CFLAGS += $(if $(DEBUG),-g -O0 -DDEBUG,-O2) $(VERSION) -D_LIN_ -Wall -c
LDFLAGS += $(if $(DEBUG),-g  -rdynamic,) -lpthread -lvncserver -lvzctl2

OBJS = \
	console.o \
	main.o \
	util.o \
	vt100.o

BINARY=prl_vzvncserver_app

all: depend $(BINARY)

$(BINARY): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

depend:
	$(CC) $(CFLAGS) -M $(OBJS:.o=.c) > depend

install:
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(BINARY) $(DESTDIR)/usr/bin

clean:
	rm -f *.o $(BINARY) depend

.PHONY: all install clean depend
