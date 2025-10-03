# Makefile for TPCL Printer Application
#
# Build the TPCL printer application using PAPPL from external/pappl

# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 -g -I../external/pappl -I../external/pappl/pappl
LDFLAGS = -L../external/pappl/pappl -Wl,-rpath,$(shell pwd)/../external/pappl/pappl
LIBS = -lpappl -lcups -lpthread -lm

# Get additional flags from PAPPL's build
PAPPL_LIBS = -lavahi-common -lavahi-client -lssl -lcrypto -ljpeg -lpng16 -lusb-1.0 -lpam -lz

# Target executable
TARGET = tpcl-printer-app

# Source files
SOURCES = tpcl-printer-app.c tpcl-driver.c
OBJECTS = $(SOURCES:.c=.o)

# Headers
HEADERS = tpcl-driver.h tpcl-media.h

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(PAPPL_LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/usr/local/share/tpcl-printer-app
	install -d $(DESTDIR)/var/lib/tpcl-printer-app
	install -d $(DESTDIR)/var/spool/tpcl-printer-app

help:
	@echo "TPCL Printer Application Build"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build the printer application (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  install - Install to system directories"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Prerequisites:"
	@echo "  - PAPPL must be built in ../external/pappl"
	@echo "  - Run 'make' in external/pappl first if not done"
