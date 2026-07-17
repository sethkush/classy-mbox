# mboxflash — native arm64 macOS firmware flasher for the original Digidesign Mbox.
# Speaks standard USB DFU 1.0 plus one custom vendor request to enter DFU mode.

CC       = clang
CFLAGS   = -O2 -Wall -Wextra -Wno-nullability-completeness -fobjc-arc \
           -mmacosx-version-min=12.0 -fmodules
LDFLAGS  = -framework Foundation -framework IOKit -framework CoreFoundation

BIN      = mboxflash
SRCS     = main.m dfu.m payload.m
OBJS     = $(SRCS:.m=.o)

.PHONY: all clean install
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.m
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/$(BIN)
