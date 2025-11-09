# Makefile for TPCL Printer Application
#
# This file is not meant to run standalone. Use Makefile in project root.

# Directories
PAPPL_DIR = ../external/pappl
BINDIR_SRC = ../bin
PREFIX ?= /usr/local
BINDIR_INSTALL = $(PREFIX)/bin

# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 -g -I$(PAPPL_DIR) -I$(PAPPL_DIR)/pappl
ifdef package-build
# For package builds: statically link PAPPL
LDFLAGS =
PAPPL_LIB = $(PAPPL_DIR)/pappl/libpappl.a
else
# For development builds: use rpath for local PAPPL
LDFLAGS = -L$(PAPPL_DIR)/pappl -Wl,-rpath,$(shell cd .. && pwd)/external/pappl/pappl
PAPPL_LIB = -lpappl
endif
LIBS = $(PAPPL_LIB) -lcups -lpthread -lm

# Get additional flags from PAPPL's build
PAPPL_LIBS = -lavahi-common -lavahi-client -lssl -lcrypto -ljpeg -lpng16 -lusb-1.0 -lpam -lz

# Target executable
TARGET = tpcl-printer-app
TARGET_PATH = $(BINDIR_SRC)/$(TARGET)

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
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "ERROR: Installation requires root privileges."; \
		echo "Please run: sudo make install"; \
		exit 1; \
	fi
	@echo "Installing $(TARGET) to $(DESTDIR)$(BINDIR_INSTALL)/..."
	install -d $(DESTDIR)$(BINDIR_INSTALL)
	install -m 755 $(TARGET_PATH) $(DESTDIR)$(BINDIR_INSTALL)/
	@echo ""
	@echo "Installation complete!"
	@echo ""
	@echo "The binary has been installed to: $(DESTDIR)$(BINDIR_INSTALL)/$(TARGET)"
	@echo ""
	@echo "Runtime directories will be created automatically by PAPPL when run as root:"
	@echo "  - State file: /var/lib/$(TARGET).state"
	@echo "  - Spool directory: /var/spool/$(TARGET)/"
	@echo ""

uninstall:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "ERROR: Uninstallation requires root privileges."; \
		echo "Please run: sudo make uninstall"; \
		exit 1; \
	fi
	@echo "Uninstalling $(TARGET) from $(DESTDIR)$(BINDIR_INSTALL)/..."
	rm -f $(DESTDIR)$(BINDIR_INSTALL)/$(TARGET)
	@echo ""
	@echo "Uninstallation complete!"
	@echo ""
	@echo "Note: Data directories are preserved and must be removed manually if desired:"
	@echo "  - State file: /var/lib/$(TARGET).state"
	@echo "  - Spool directory: /var/spool/$(TARGET)/"
	@echo ""
