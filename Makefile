
# TPCL Printer Application - Local Development Makefile
# This Makefile is for LOCAL development only.
# Package builds (DEB/RPM) have their own independent build logic.

# Directories
SRCDIR = src
BINDIR = bin
PAPPL_DIR = external/pappl

# Build options
SINGLE_ARCH ?= 0

.PHONY: all full pappl-init clean install uninstall help

# Default target: build the application (statically linked)
all:
	@$(MAKE) pappl-init
	@if [ ! -f "$(SRCDIR)/icon-48.h" ] || [ ! -f "$(SRCDIR)/icon-128.h" ] || [ ! -f "$(SRCDIR)/icon-512.h" ]; then \
		echo "Generating missing icon headers..."; \
		./scripts/generate-icon-headers.sh; \
	fi
	@echo "Generating version header..."
	@./scripts/generate-version.sh
	@mkdir -p $(BINDIR)
	@$(MAKE) -C $(SRCDIR) all

# Full build: clean and rebuild everything including PAPPL
full:
	@$(MAKE) clean
	@echo "Generating embedded icon headers..."
	@./scripts/generate-icon-headers.sh
	@echo "Generating version header..."
	@./scripts/generate-version.sh
	@mkdir -p $(BINDIR)
	@echo "Rebuilding PAPPL from scratch..."
	@$(MAKE) pappl-build
	@$(MAKE) -C $(SRCDIR) all

# Initialize PAPPL submodule and build if needed
pappl-init:
	@if [ ! -f "$(PAPPL_DIR)/configure" ]; then \
		echo "Initializing PAPPL submodule..."; \
		git submodule update --init --recursive; \
	fi
	@if [ ! -f "$(PAPPL_DIR)/pappl/libpappl.a" ]; then \
		echo "Building PAPPL..."; \
		$(MAKE) pappl-build; \
	fi

# Build PAPPL with appropriate architecture flags
pappl-build:
	@echo "Patching PAPPL translations with custom strings..."
	@./scripts/patch-translations.sh
	@echo "Configuring and building PAPPL..."
	@cd $(PAPPL_DIR) && \
	if [ "$(SINGLE_ARCH)" = "1" ]; then \
		ARCH=$$(uname -m); \
		echo "Building PAPPL for single architecture: $$ARCH"; \
		./configure CFLAGS="-arch $$ARCH" LDFLAGS="-arch $$ARCH" && $(MAKE); \
	elif [ -n "$$ARCHFLAGS" ]; then \
		echo "Building PAPPL with ARCHFLAGS=$$ARCHFLAGS"; \
		./configure && $(MAKE) ARCHFLAGS="$$ARCHFLAGS"; \
	else \
		echo "Building PAPPL with default configuration"; \
		./configure && $(MAKE); \
	fi

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@$(MAKE) -C $(SRCDIR) clean
	@rm -rf $(BINDIR)
	@rm -f $(SRCDIR)/icon-*.h
	@rm -f $(SRCDIR)/version.h
	@echo "Resetting git-modified files..."
	@git checkout -- tpcl-printer-app.spec
	@git checkout -- debian/changelog
	@echo "Resetting PAPPL submodule..."
	@git submodule update --init --force external/pappl
	@echo "Clean complete."

# Install the application to system directories
install:
	@$(MAKE) -C $(SRCDIR) install

# Uninstall the application from system directories
uninstall:
	@$(MAKE) -C $(SRCDIR) uninstall

# Show help message
help:
	@echo "TPCL Printer Application - Local Development Build System"
	@echo ""
	@echo "NOTE: This Makefile is for LOCAL development only."
	@echo "      Package builds (DEB/RPM) use debian/rules and .spec files."
	@echo ""
	@echo "Available targets:"
	@echo "  make            - Build the application (default, static linking)"
	@echo "  make full       - Clean and rebuild everything including PAPPL"
	@echo "  make clean      - Remove build artifacts and reset git state"
	@echo "  make install    - Install to system directories (requires sudo)"
	@echo "  make uninstall  - Remove from system directories (requires sudo)"
	@echo "  make help       - Show this help message"
	@echo ""
	@echo "Build options:"
	@echo "  SINGLE_ARCH=1   - Build for single architecture only (useful for macOS)"
	@echo "  ARCHFLAGS=...   - Override architecture flags (e.g., ARCHFLAGS=\"-arch x86_64\")"
	@echo ""
	@echo "Examples:"
	@echo "  make                      # Build with default settings"
	@echo "  make SINGLE_ARCH=1        # Force single-architecture build"
	@echo "  make full                 # Clean rebuild of everything"
	@echo ""
	@echo "Build output:"
	@echo "  Binary: $(BINDIR)/tpcl-printer-app"
	@echo ""
	@echo "Dependencies:"
	@echo "  - Git (for PAPPL submodule)"
	@echo "  - CUPS development libraries"
	@echo "  - Libraries: openssl, jpeg, png, usb, pam"
	@echo "  - macOS: mDNSResponder (built-in)"
	@echo "  - Linux: Avahi client libraries"
	@echo ""
	@echo "Quick start:"
	@echo "  1. Run 'make' to build"
	@echo "  2. Run 'sudo $(BINDIR)/tpcl-printer-app server -o log-level=debug'"
	@echo ""

