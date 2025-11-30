# Makefile for TPCL Printer Application Source
#
# Can be called standalone (e.g., from package builds) or via parent Makefile.
# Requires PAPPL to be already built in ../external/pappl.

# Directories
PAPPL_DIR = ../external/pappl
BINDIR_SRC = ../bin
PREFIX ?= /usr/local
BINDIR_INSTALL = $(PREFIX)/bin

# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 -g -I$(PAPPL_DIR) -I$(PAPPL_DIR)/pappl
# Statically link PAPPL
LDFLAGS =
PAPPL_LIB = $(PAPPL_DIR)/pappl/libpappl.a
LIBS = $(PAPPL_LIB) -lcups -lpthread -lm

# Get additional flags from PAPPL's build
# Extract LIBS from PAPPL's Makedefs (excluding PAPPL itself and CUPS which we already link)
# Note: Makedefs is created by PAPPL's configure; if missing, PAPPL needs to be built first
PAPPL_LIBS = $(shell if [ -f "$(PAPPL_DIR)/Makedefs" ]; then \
	grep '^LIBS[[:space:]]*=' $(PAPPL_DIR)/Makedefs | \
	sed 's/^LIBS[[:space:]]*=[[:space:]]*//' | \
	sed 's/-lcups//' | sed 's/-lpthread//' | sed 's/-lm//'; \
	fi)

# Target executable
TARGET = tpcl-printer-app
TARGET_PATH = $(BINDIR_SRC)/$(TARGET)

# Source files
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

# Headers
HEADERS := $(wildcard *.h)

.PHONY: all clean install uninstall check-pappl

all: check-pappl $(TARGET_PATH)

check-pappl:
	@if [ ! -f "$(PAPPL_LIB)" ]; then \
		echo "ERROR: PAPPL library not found at $(PAPPL_LIB)"; \
		echo "PAPPL must be built before building tpcl-printer-app."; \
		echo ""; \
		echo "For local builds: Run 'make' from the project root"; \
		echo "For package builds: Ensure PAPPL is built first"; \
		exit 1; \
	fi

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
