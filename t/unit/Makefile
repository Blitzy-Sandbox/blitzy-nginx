# Copyright (C) Igor Sysoev
# Copyright (C) Nginx, Inc.

# Standalone unit-test runner for the centralized HTTP status registry API
# (src/http/ngx_http_status.c / .h).  It compiles the shipping status source
# together with the test driver using the same compiler flags and include
# paths as the main nginx build, then runs the resulting program.
#
# The project must be configured first (auto/configure), which produces
# objs/ngx_auto_config.h.  The NGX_HTTP_STATUS_VALIDATION setting is taken
# from that generated header, so these tests always match whichever build
# variant was configured (default or --with-http_status_validation).
#
# Usage, from the repository root after auto/configure:
#     make check                 # top-level target delegates here
#     make -C t/unit check       # or invoke this Makefile directly
#     make -C t/unit clean

NGX_ROOT =	../..

CC =		cc
CFLAGS =	-pipe -O -W -Wall -Wpointer-arith -Wno-unused-parameter -Werror -g

NGX_INCS =	-I $(NGX_ROOT)/src/core \
		-I $(NGX_ROOT)/src/event \
		-I $(NGX_ROOT)/src/event/modules \
		-I $(NGX_ROOT)/src/event/quic \
		-I $(NGX_ROOT)/src/os/unix \
		-I $(NGX_ROOT)/objs \
		-I $(NGX_ROOT)/src/http \
		-I $(NGX_ROOT)/src/http/v2 \
		-I $(NGX_ROOT)/src/http/v3 \
		-I $(NGX_ROOT)/src/http/modules

NGX_AUTO_CONFIG =	$(NGX_ROOT)/objs/ngx_auto_config.h
NGX_STATUS_SRC =	$(NGX_ROOT)/src/http/ngx_http_status.c
TEST_SRC =		ngx_http_status_test.c
TEST_BIN =		ngx_http_status_test

.PHONY:	check clean

check:	$(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN):	$(NGX_AUTO_CONFIG) $(TEST_SRC) $(NGX_STATUS_SRC)
	$(CC) $(CFLAGS) $(NGX_INCS) -o $@ $(TEST_SRC) $(NGX_STATUS_SRC)

$(NGX_AUTO_CONFIG):
	@echo "error: $(NGX_AUTO_CONFIG) is missing." >&2
	@echo "       run auto/configure from the repository root first." >&2
	@exit 1

clean:
	rm -f $(TEST_BIN)
