TARGET=recordthepiano

CC=gcc
CFLAGS=-g -O2 -Wall
LDFLAGS=-lportaudio -lpthread -lm -lFLAC
LD=gcc
UNAME=$(shell uname)

SOURCES =	\
    recorder.c	\

ifndef DESTDIR
    DESTDIR := /usr/local
endif

OBJECTS=$(SOURCES:%.c=build/%.o)

default : $(TARGET)

build/%.o : %.c build
	$(CC) $(CFLAGS) -c -o $@ $<
	$(CC) -MM $(CLFAGS) $< > build/$*.d

$(TARGET): $(OBJECTS)
	$(LD) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

.PHONY : clean
clean: 
	rm -Rf build/*
	rm -f $(TARGET)

build:
	@mkdir -p build

install: $(TARGET)
	mkdir -p /var/lib/recordthepiano
	chown recordthepiano.recordthepiano /var/lib/recordthepiano -R
	mkdir -p /var/log/recordthepiano
	chown recordthepiano.recordthepiano /var/log/recordthepiano -R
	install -m 0755 $(TARGET) $(DESTDIR)/bin/$(TARGET)
	install -m 0755 recordthepiano_upload $(DESTDIR)/bin/recordthepiano_upload
	install -m 0755 init.d/recordthepiano /etc/init.d/recordthepiano
