# Makefile for TPCL Printer Application
#
# This file is not meant to run standalone. Use Makefile in project root.

# Directories
BINDIR = ../bin
PAPPL_DIR = ../external/pappl

# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 -g -I$(PAPPL_DIR) -I$(PAPPL_DIR)/pappl
LDFLAGS = -L$(PAPPL_DIR)/pappl -Wl,-rpath,$(shell cd .. && pwd)/external/pappl/pappl
LIBS = -lpappl -lcups -lpthread -lm

# Get additional flags from PAPPL's build
PAPPL_LIBS = -lavahi-common -lavahi-client -lssl -lcrypto -ljpeg -lpng16 -lusb-1.0 -lpam -lz

# Target executable
TARGET = tpcl-printer-app
TARGET_PATH = $(BINDIR)/$(TARGET)

# Source files
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

# Headers
HEADERS := $(wildcard *.h)

.PHONY: all clean install uninstall

all: $(TARGET_PATH)

$(TARGET_PATH): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(PAPPL_LIBS)
	@echo "Built: $(TARGET_PATH)"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS)

install: $(TARGET_PATH)
	@echo "Installing $(TARGET) to system directories..."
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET_PATH) $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/usr/local/share/tpcl-printer-app
	install -d $(DESTDIR)/var/lib/tpcl-printer-app
	install -d $(DESTDIR)/var/spool/tpcl-printer-app
	@echo "Installation complete."

uninstall:
	@echo "Uninstalling $(TARGET) from system directories..."
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -rf $(DESTDIR)/usr/local/share/tpcl-printer-app
	rm -rf $(DESTDIR)/var/lib/tpcl-printer-app
	rm -rf $(DESTDIR)/var/spool/tpcl-printer-app
	@echo "Uninstallation complete."
