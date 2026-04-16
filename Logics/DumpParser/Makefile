# Open DUMP Viewer for Oracle database - C Library Build (Linux / macOS)
#
# Usage:
#   make              # Build shared library
#   make clean        # Remove build artifacts
#   make install      # Install to /usr/local/lib (requires sudo)

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -fPIC -Wno-format-truncation
DEFS    = -DUTF8 -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -shared

# macOS uses .dylib, Linux uses .so
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LIBEXT  = dylib
  LDFLAGS += -dynamiclib -install_name @rpath/libodv_dumpparser.$(LIBEXT)
else
  LIBEXT  = so
  LDFLAGS += -Wl,-soname,libodv_dumpparser.$(LIBEXT)
endif

TARGET  = libodv_dumpparser.$(LIBEXT)

SRCS    = odv_api.c odv_detect.c odv_expdp.c odv_exp.c odv_record.c \
          odv_number.c odv_datetime.c odv_charset.c odv_xml.c odv_csv.c odv_sql.c

OBJS    = $(SRCS:.c=.o)

PREFIX  ?= /usr/local

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEFS) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(PREFIX)/lib
	install -m 755 $(TARGET) $(PREFIX)/lib/
	install -d $(PREFIX)/include/odv
	install -m 644 odv_api.h $(PREFIX)/include/odv/
