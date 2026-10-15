# Resolve the native platform before Make_defines.mk selects tools and flags.
# Explicit targets are preserved; auto never guesses a cross-compilation target.
TSYS ?= auto
TSYS_REQUESTED := $(strip $(TSYS))
UNAME ?= uname

ifneq ($(filter 1,$(TARGET_LINUX_AARCH64) $(TARGET_RGB30) $(TARGET_PORTMASTER)),)
ifneq ($(filter $(TSYS_REQUESTED),auto linux),$(TSYS_REQUESTED))
$(error AArch64/handheld presets require TSYS=auto or TSYS=linux)
endif
override TSYS := linux
else ifeq ($(TSYS_REQUESTED),auto)
# Native Windows make can run without uname. Avoid POSIX redirection in cmd.exe.
ifeq ($(OS),Windows_NT)
TSYS_DETECTED := windows_mingw
else
TSYS_UNAME := $(strip $(shell $(UNAME) -s 2>/dev/null))
ifeq ($(TSYS_UNAME),Linux)
TSYS_DETECTED := linux
else ifeq ($(TSYS_UNAME),Darwin)
TSYS_DETECTED := macos
else ifneq ($(filter MINGW% MSYS% CYGWIN%,$(TSYS_UNAME)),)
TSYS_DETECTED := windows_mingw
else
$(error Cannot detect native platform '$(TSYS_UNAME)'; set TSYS=linux, windows_mingw, macos, or emscripten explicitly)
endif
endif
# Command-line TSYS=auto must resolve too; an ordinary assignment cannot replace it.
override TSYS := $(TSYS_DETECTED)
else ifeq ($(TSYS_REQUESTED),osx)
override TSYS := macos
endif

ifneq ($(words $(TSYS)),1)
$(error Set exactly one TSYS target: auto, linux, windows_mingw, macos, osx, or emscripten)
endif
ifeq ($(filter $(TSYS),linux windows_mingw macos emscripten),)
$(error Unsupported TSYS '$(TSYS)'; use auto, linux, windows_mingw, macos, osx, or emscripten)
endif
