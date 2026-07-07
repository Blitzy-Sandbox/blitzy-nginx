
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


/*
 * Unit tests for the centralized HTTP status registry API declared in
 * src/http/ngx_http_status.h and implemented in src/http/ngx_http_status.c.
 *
 * The tests link the real status translation unit (compiled from the shipping
 * source with the build's own flags by t/unit/Makefile), so they assert the
 * exact bytes and behavior that ship.  The five nginx symbols the status unit
 * references but that these tests do not exercise -- the logging core and the
 * $request_id variable lookups -- are provided below as link-time stubs, so no
 * other part of the nginx runtime has to be linked in.
 *
 * The same source is compiled once per build variant: t/unit/Makefile derives
 * NGX_HTTP_STATUS_VALIDATION from objs/ngx_auto_config.h, so the strict-mode
 * assertions below (guarded by #if (NGX_HTTP_STATUS_VALIDATION)) match the
 * variant of the status unit under test.  The process exit status is zero only
 * when every assertion passes, so the runner ("make check") fails the build on
 * any regression.
 *
 * Diagnostic messages print via the C library's printf(), so every integer is
 * cast to unsigned long and formatted with %lu; this keeps the harness warning
 * free under -Werror on both LP64 and ILP32 targets (nginx's own %ui/%uz
 * conversions belong to ngx_sprintf(), not to printf()).
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_status.h>

#include <stdio.h>
#include <string.h>
#include <limits.h>


/*
 * Link-time stubs.  The status unit references these five nginx symbols; the
 * tests never depend on their behavior, so each is a minimal no-op whose
 * signature matches the nginx headers included above.  Logging folds to
 * nothing and every $request_id lookup reports "not found", which drives
 * ngx_http_status_log() down its empty-id path without touching the variable
 * subsystem.
 */

void
ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...)
{
    (void) level; (void) log; (void) err; (void) fmt;
}


ngx_uint_t
ngx_hash_key(u_char *data, size_t len)
{
    (void) data; (void) len;
    return 0;
}


ngx_int_t
ngx_http_get_variable_index(ngx_conf_t *cf, ngx_str_t *name)
{
    (void) cf; (void) name;
    return NGX_ERROR;
}


ngx_http_variable_value_t *
ngx_http_get_indexed_variable(ngx_http_request_t *r, ngx_uint_t index)
{
    (void) r; (void) index;
    return NULL;
}


ngx_http_variable_value_t *
ngx_http_get_variable(ngx_http_request_t *r, ngx_str_t *name, ngx_uint_t key)
{
    (void) r; (void) name; (void) key;
    return NULL;
}


/* Minimal assertion framework: count and report; never abort on first failure. */

static ngx_uint_t  ngx_test_run;
static ngx_uint_t  ngx_test_pass;
static ngx_uint_t  ngx_test_fail;


#define NGX_TEST_CHECK(cond, ...)                                             \
    do {                                                                      \
        ngx_test_run++;                                                       \
        if (cond) {                                                           \
            ngx_test_pass++;                                                  \
        } else {                                                              \
            ngx_test_fail++;                                                  \
            printf("  FAIL: ");                                               \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)


static ngx_uint_t
ngx_test_str_eq(ngx_str_t s, const char *c)
{
    size_t  len;

    len = strlen(c);

    return s.len == len && s.data != NULL && memcmp(s.data, c, len) == 0;
}


/*
 * Ground-truth expectations, transcribed from src/http/ngx_http_status.c and
 * cross-checked against the historical stock nginx status-line table.  Each
 * seeded ("ROW") code carries its full wire status line and its bare reason
 * phrase; the reason is the line past the invariant four-character "NNN "
 * prefix.
 */

typedef struct {
    ngx_uint_t   code;
    const char  *line;
    const char  *reason;
} ngx_test_row_t;


static ngx_test_row_t  ngx_test_rows[] = {
    { 200, "200 OK", "OK" },
    { 201, "201 Created", "Created" },
    { 202, "202 Accepted", "Accepted" },
    { 204, "204 No Content", "No Content" },
    { 206, "206 Partial Content", "Partial Content" },
    { 301, "301 Moved Permanently", "Moved Permanently" },
    { 302, "302 Moved Temporarily", "Moved Temporarily" },
    { 303, "303 See Other", "See Other" },
    { 304, "304 Not Modified", "Not Modified" },
    { 307, "307 Temporary Redirect", "Temporary Redirect" },
    { 308, "308 Permanent Redirect", "Permanent Redirect" },
    { 400, "400 Bad Request", "Bad Request" },
    { 401, "401 Unauthorized", "Unauthorized" },
    { 402, "402 Payment Required", "Payment Required" },
    { 403, "403 Forbidden", "Forbidden" },
    { 404, "404 Not Found", "Not Found" },
    { 405, "405 Not Allowed", "Not Allowed" },
    { 406, "406 Not Acceptable", "Not Acceptable" },
    { 408, "408 Request Time-out", "Request Time-out" },
    { 409, "409 Conflict", "Conflict" },
    { 410, "410 Gone", "Gone" },
    { 411, "411 Length Required", "Length Required" },
    { 412, "412 Precondition Failed", "Precondition Failed" },
    { 413, "413 Request Entity Too Large", "Request Entity Too Large" },
    { 414, "414 Request-URI Too Large", "Request-URI Too Large" },
    { 415, "415 Unsupported Media Type", "Unsupported Media Type" },
    { 416, "416 Requested Range Not Satisfiable",
           "Requested Range Not Satisfiable" },
    { 421, "421 Misdirected Request", "Misdirected Request" },
    { 429, "429 Too Many Requests", "Too Many Requests" },
    { 500, "500 Internal Server Error", "Internal Server Error" },
    { 501, "501 Not Implemented", "Not Implemented" },
    { 502, "502 Bad Gateway", "Bad Gateway" },
    { 503, "503 Service Temporarily Unavailable",
           "Service Temporarily Unavailable" },
    { 504, "504 Gateway Time-out", "Gateway Time-out" },
    { 505, "505 HTTP Version Not Supported", "HTTP Version Not Supported" },
    { 507, "507 Insufficient Storage", "Insufficient Storage" }
};

static const ngx_uint_t  ngx_test_nrows =
    sizeof(ngx_test_rows) / sizeof(ngx_test_rows[0]);


/* Seeded-but-numeric-only ("GAP") codes: registered class members with no line. */

static ngx_uint_t  ngx_test_gaps[] = {
    203, 205, 300, 305, 306, 407, 417, 418, 419, 420,
    422, 423, 424, 425, 426, 427, 428, 506
};

static const ngx_uint_t  ngx_test_ngaps =
    sizeof(ngx_test_gaps) / sizeof(ngx_test_gaps[0]);


/* The complete set of heuristically cacheable codes (RFC 9110 15.1). */

static ngx_uint_t  ngx_test_cacheable[] = {
    200, 203, 204, 206, 301, 308, 404, 405, 410, 414, 501
};

static const ngx_uint_t  ngx_test_ncacheable =
    sizeof(ngx_test_cacheable) / sizeof(ngx_test_cacheable[0]);


static ngx_uint_t
ngx_test_in_list(ngx_uint_t code, ngx_uint_t *list, ngx_uint_t n)
{
    ngx_uint_t  i;

    for (i = 0; i < n; i++) {
        if (list[i] == code) {
            return 1;
        }
    }

    return 0;
}


/*
 * A request scaffold carrying a valid connection and a level-0 log, so every
 * logging macro guard inside the status unit is false: ngx_http_status_set()
 * exercises its real logic with no logging side effects and no NULL deref.
 */

static ngx_log_t           ngx_test_log;
static ngx_connection_t    ngx_test_conn;
static ngx_http_request_t  ngx_test_request;


static void
ngx_test_register(void)
{
    NGX_TEST_CHECK(ngx_http_status_register() == NGX_OK,
                   "register() first call must return NGX_OK");
    NGX_TEST_CHECK(ngx_http_status_register() == NGX_OK,
                   "register() must be idempotent (second call)");
    NGX_TEST_CHECK(ngx_http_status_register() == NGX_OK,
                   "register() must be idempotent (third call)");
}


static void
ngx_test_line_and_reason(void)
{
    ngx_uint_t  i;
    ngx_str_t   line, reason;

    /* Every seeded ROW: exact line, exact reason, reason == line + 4. */

    for (i = 0; i < ngx_test_nrows; i++) {
        line = ngx_http_status_line(ngx_test_rows[i].code);
        NGX_TEST_CHECK(ngx_test_str_eq(line, ngx_test_rows[i].line),
                       "line(%lu) mismatch (len=%lu)",
                       (unsigned long) ngx_test_rows[i].code,
                       (unsigned long) line.len);

        reason = ngx_http_status_reason(ngx_test_rows[i].code);
        NGX_TEST_CHECK(ngx_test_str_eq(reason, ngx_test_rows[i].reason),
                       "reason(%lu) mismatch",
                       (unsigned long) ngx_test_rows[i].code);

        NGX_TEST_CHECK(line.len == reason.len + (sizeof("000 ") - 1)
                       && reason.data == line.data + (sizeof("000 ") - 1),
                       "reason/line prefix invariant broken for %lu",
                       (unsigned long) ngx_test_rows[i].code);
    }

    /* GAP codes: seeded but numeric-only, so both line and reason are empty. */

    for (i = 0; i < ngx_test_ngaps; i++) {
        line = ngx_http_status_line(ngx_test_gaps[i]);
        NGX_TEST_CHECK(line.len == 0,
                       "line(gap %lu) must be empty (len=%lu)",
                       (unsigned long) ngx_test_gaps[i],
                       (unsigned long) line.len);

        reason = ngx_http_status_reason(ngx_test_gaps[i]);
        NGX_TEST_CHECK(reason.len == 0,
                       "reason(gap %lu) must be empty (len=%lu)",
                       (unsigned long) ngx_test_gaps[i],
                       (unsigned long) reason.len);
    }
}


static void
ngx_test_unknown(void)
{
    ngx_uint_t  i, n;
    ngx_str_t   line, reason;

    /* Class-edge, out-of-class, and out-of-range codes: no line, no reason. */

    static ngx_uint_t  unknown[] = {
        0, 1, 99, 100, 101, 199, 207, 208, 250, 299, 309, 310, 399,
        430, 499, 508, 509, 599, 600, 999, (ngx_uint_t) UINT_MAX
    };

    n = sizeof(unknown) / sizeof(unknown[0]);

    for (i = 0; i < n; i++) {
        line = ngx_http_status_line(unknown[i]);
        NGX_TEST_CHECK(line.len == 0, "line(unknown %lu) must be empty",
                       (unsigned long) unknown[i]);

        reason = ngx_http_status_reason(unknown[i]);
        NGX_TEST_CHECK(reason.len == 0, "reason(unknown %lu) must be empty",
                       (unsigned long) unknown[i]);
    }
}


static void
ngx_test_is_cacheable(void)
{
    ngx_uint_t  code, got, expect;

    /* Exhaustive sweep: cacheable is true for exactly the eleven codes. */

    for (code = 0; code <= 700; code++) {
        got = ngx_http_status_is_cacheable(code);
        expect = ngx_test_in_list(code, ngx_test_cacheable,
                                  ngx_test_ncacheable) ? 1 : 0;
        NGX_TEST_CHECK(got == expect,
                       "is_cacheable(%lu)=%lu expected %lu",
                       (unsigned long) code, (unsigned long) got,
                       (unsigned long) expect);
    }

    NGX_TEST_CHECK(ngx_http_status_is_cacheable((ngx_uint_t) UINT_MAX) == 0,
                   "is_cacheable(UINT_MAX) must be 0");

    /* 203 is a numeric-only GAP yet still flagged cacheable. */

    NGX_TEST_CHECK(ngx_http_status_is_cacheable(203) == 1,
                   "203 must be cacheable");
    NGX_TEST_CHECK(ngx_http_status_reason(203).len == 0,
                   "203 reason must be empty even though it is cacheable");
}


static void
ngx_test_validate(void)
{
    /*
     * validate() enforces the RFC 9110 100..599 range only in the validation
     * build; the default build folds it to a constant NGX_OK.  Both variants
     * are asserted here so the suite is correct whichever object it links.
     */

#if (NGX_HTTP_STATUS_VALIDATION)
    NGX_TEST_CHECK(ngx_http_status_validate(99) == NGX_ERROR,
                   "validate(99) must be NGX_ERROR (strict)");
    NGX_TEST_CHECK(ngx_http_status_validate(100) == NGX_OK,
                   "validate(100) must be NGX_OK");
    NGX_TEST_CHECK(ngx_http_status_validate(200) == NGX_OK,
                   "validate(200) must be NGX_OK");
    NGX_TEST_CHECK(ngx_http_status_validate(599) == NGX_OK,
                   "validate(599) must be NGX_OK");
    NGX_TEST_CHECK(ngx_http_status_validate(600) == NGX_ERROR,
                   "validate(600) must be NGX_ERROR (strict)");
    NGX_TEST_CHECK(ngx_http_status_validate(0) == NGX_ERROR,
                   "validate(0) must be NGX_ERROR (strict)");
    NGX_TEST_CHECK(ngx_http_status_validate((ngx_uint_t) UINT_MAX) == NGX_ERROR,
                   "validate(UINT_MAX) must be NGX_ERROR (strict)");
#else
    NGX_TEST_CHECK(ngx_http_status_validate(99) == NGX_OK,
                   "validate(99) must be NGX_OK (default permissive)");
    NGX_TEST_CHECK(ngx_http_status_validate(100) == NGX_OK,
                   "validate(100) must be NGX_OK");
    NGX_TEST_CHECK(ngx_http_status_validate(599) == NGX_OK,
                   "validate(599) must be NGX_OK");
    NGX_TEST_CHECK(ngx_http_status_validate(600) == NGX_OK,
                   "validate(600) must be NGX_OK (default permissive)");
    NGX_TEST_CHECK(ngx_http_status_validate(0) == NGX_OK,
                   "validate(0) must be NGX_OK (default permissive)");
    NGX_TEST_CHECK(ngx_http_status_validate((ngx_uint_t) UINT_MAX) == NGX_OK,
                   "validate(UINT_MAX) must be NGX_OK (default permissive)");
#endif
}


static void
ngx_test_set(void)
{
    /* NULL request guard. */

    NGX_TEST_CHECK(ngx_http_status_set(NULL, 200) == NGX_ERROR,
                   "set(NULL, 200) must return NGX_ERROR");

    /* Locally generated valid code: writes the field, returns NGX_OK. */

    ngx_test_request.upstream = NULL;
    ngx_test_request.headers_out.status = 0;
    NGX_TEST_CHECK(ngx_http_status_set(&ngx_test_request, 200) == NGX_OK,
                   "set(local, 200) must return NGX_OK");
    NGX_TEST_CHECK(ngx_test_request.headers_out.status == 200,
                   "set(local, 200) must write the status field");

    /*
     * Upstream pass-through: with r->upstream set, even out-of-range backend
     * codes are written unchanged and unvalidated in both build variants.
     */

    ngx_test_request.upstream = (ngx_http_upstream_t *) &ngx_test_request;

    ngx_test_request.headers_out.status = 0;
    NGX_TEST_CHECK(ngx_http_status_set(&ngx_test_request, 700) == NGX_OK,
                   "set(upstream, 700) must pass through (NGX_OK)");
    NGX_TEST_CHECK(ngx_test_request.headers_out.status == 700,
                   "set(upstream, 700) must write 700 unvalidated");

    ngx_test_request.headers_out.status = 0;
    NGX_TEST_CHECK(ngx_http_status_set(&ngx_test_request, 999) == NGX_OK,
                   "set(upstream, 999) must pass through (NGX_OK)");
    NGX_TEST_CHECK(ngx_test_request.headers_out.status == 999,
                   "set(upstream, 999) must write 999 unvalidated");

    ngx_test_request.upstream = NULL;

    /*
     * Rejection semantics are variant-specific.  In the validation build an
     * out-of-range local code is rejected without overwriting the field and
     * bumps the rejection counter; a valid local code bumps its class counter.
     * In the default build the same code passes through permissively.
     */

    ngx_test_request.headers_out.status = 200;      /* sentinel */

#if (NGX_HTTP_STATUS_VALIDATION)
    {
        ngx_atomic_t  before;

        before = ngx_http_status_counters[NGX_HTTP_STATUS_REJECTED];
        NGX_TEST_CHECK(ngx_http_status_set(&ngx_test_request, 700) == NGX_ERROR,
                       "set(local, 700) must be rejected (strict)");
        NGX_TEST_CHECK(ngx_test_request.headers_out.status == 200,
                       "rejected set() must not overwrite the status field");
        NGX_TEST_CHECK(
            ngx_http_status_counters[NGX_HTTP_STATUS_REJECTED] == before + 1,
            "rejection must increment the rejected counter by one");
    }

    {
        ngx_atomic_t  before;

        before = ngx_http_status_counters[NGX_HTTP_STATUS_CLASS_2XX];
        ngx_test_request.headers_out.status = 0;
        NGX_TEST_CHECK(ngx_http_status_set(&ngx_test_request, 201) == NGX_OK,
                       "set(local, 201) must return NGX_OK");
        NGX_TEST_CHECK(
            ngx_http_status_counters[NGX_HTTP_STATUS_CLASS_2XX] == before + 1,
            "valid 2xx set must increment the 2xx class counter by one");
    }
#else
    NGX_TEST_CHECK(ngx_http_status_set(&ngx_test_request, 700) == NGX_OK,
                   "set(local, 700) must be NGX_OK (default permissive)");
    NGX_TEST_CHECK(ngx_test_request.headers_out.status == 700,
                   "default set(local, 700) must write the field");
#endif
}


int
main(void)
{
    ngx_test_conn.log = &ngx_test_log;          /* log_level 0 => macros no-op */
    ngx_test_request.connection = &ngx_test_conn;

#if (NGX_HTTP_STATUS_VALIDATION)
    printf("=== ngx_http_status unit tests: VALIDATION build "
           "(NGX_HTTP_STATUS_VALIDATION=1) ===\n");
#else
    printf("=== ngx_http_status unit tests: DEFAULT build "
           "(validation off) ===\n");
#endif

    ngx_test_register();
    ngx_test_line_and_reason();
    ngx_test_unknown();
    ngx_test_is_cacheable();
    ngx_test_validate();
    ngx_test_set();

    printf("---- %lu run, %lu passed, %lu failed ----\n",
           (unsigned long) ngx_test_run, (unsigned long) ngx_test_pass,
           (unsigned long) ngx_test_fail);

    return ngx_test_fail == 0 ? 0 : 1;
}
