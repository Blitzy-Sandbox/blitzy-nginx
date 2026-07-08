
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Class boundary and offset constants for the direct-index registry.  Each
 * LAST_* value is the exclusive upper bound of its class; each OFF_* value is
 * the index at which the next class begins.  The 1xx class starts at 100
 * (NGX_HTTP_CONTINUE) at index 0, so every OFF_* downstream includes the
 * leading 1xx rows.  The 3xx class starts at 300 (NGX_HTTP_SPECIAL_RESPONSE)
 * so that code has its own row.
 */

#define NGX_HTTP_STATUS_LAST_1XX   104
#define NGX_HTTP_STATUS_OFF_2XX    (NGX_HTTP_STATUS_LAST_1XX - NGX_HTTP_CONTINUE)

#define NGX_HTTP_STATUS_LAST_2XX   207
#define NGX_HTTP_STATUS_OFF_3XX    (NGX_HTTP_STATUS_LAST_2XX - NGX_HTTP_OK       \
                                    + NGX_HTTP_STATUS_OFF_2XX)

#define NGX_HTTP_STATUS_LAST_3XX   309
#define NGX_HTTP_STATUS_OFF_4XX    (NGX_HTTP_STATUS_LAST_3XX                   \
                                    - NGX_HTTP_SPECIAL_RESPONSE                \
                                    + NGX_HTTP_STATUS_OFF_3XX)

#define NGX_HTTP_STATUS_LAST_4XX   430
#define NGX_HTTP_STATUS_OFF_5XX    (NGX_HTTP_STATUS_LAST_4XX                   \
                                    - NGX_HTTP_BAD_REQUEST                     \
                                    + NGX_HTTP_STATUS_OFF_4XX)

#define NGX_HTTP_STATUS_LAST_5XX   508


/*
 * RFC 9110 section 15 back-reference for a row, packed as
 * (subsection << 8) | item so each row stays 16 bytes and the whole table
 * stays under 1 KB per worker.  For example section 15.3.1 (status 200) packs
 * to 0x0301.  Codes not defined in RFC 9110 section 15 (for example 429 from
 * RFC 6585 or 507 from RFC 4918) and undefined gap rows carry
 * NGX_HTTP_STATUS_RFC_NONE.
 */

#define NGX_HTTP_STATUS_RFC(sub, item)   ((uint16_t) (((sub) << 8) | (item)))
#define NGX_HTTP_STATUS_RFC_NONE         ((uint16_t) 0)


/*
 * Registry row constructors.  NGX_HTTP_STATUS_ROW stores the full wire status
 * line ("200 OK") so the hot-path header filter emits it with a single copy
 * and no per-response formatting; the bare reason phrase ("OK") is derived on
 * demand by ngx_http_status_reason() (see below).  NGX_HTTP_STATUS_GAP is a
 * numeric-only code that carries no status line.
 */

#define NGX_HTTP_STATUS_ROW(code, line, flags, rfc)                           \
    { (line), (uint16_t) (sizeof(line) - 1), (code), (flags), (rfc) }
#define NGX_HTTP_STATUS_GAP(code, flags, rfc)                                 \
    { NULL, 0, (code), (flags), (rfc) }


/*
 * Centralized HTTP status registry: a compile-time-constant, read-only table
 * laid out contiguously by class offset and indexed in O(1).  Populated before
 * the first worker fork and shared copy-on-write, it needs no locking.  Each
 * row stores the full wire status line ("200 OK"); gap rows carry no line, so
 * their codes render numeric-only, reproducing nginx's historical status lines
 * byte-for-byte.
 */

static const ngx_http_status_def_t  ngx_http_status_defs[] = {

    /*
     * 1xx informational (RFC 9110 15.2).  These are gap rows: they carry no
     * wire status line, so they render numeric-only and add no bytes to the
     * response.  nginx emits its 1xx responses (100 Continue, 103 Early Hints)
     * through dedicated hardcoded paths rather than this table, so seeding the
     * codes here records their class metadata without altering wire output.
     * 100 and 101 map to RFC 9110 15.2; 102 (RFC 2518) and 103 (RFC 8297) are
     * outside RFC 9110 section 15, so they carry NGX_HTTP_STATUS_RFC_NONE.
     */
    NGX_HTTP_STATUS_GAP(100, NGX_HTTP_STATUS_INFORMATIONAL,
        NGX_HTTP_STATUS_RFC(2, 1)),
    NGX_HTTP_STATUS_GAP(101, NGX_HTTP_STATUS_INFORMATIONAL,
        NGX_HTTP_STATUS_RFC(2, 2)),
    NGX_HTTP_STATUS_GAP(102, NGX_HTTP_STATUS_INFORMATIONAL,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_GAP(103, NGX_HTTP_STATUS_INFORMATIONAL,
        NGX_HTTP_STATUS_RFC_NONE),

    /* 2xx successful (RFC 9110 15.3) */
    NGX_HTTP_STATUS_ROW(200, "200 OK", NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(3, 1)),
    NGX_HTTP_STATUS_ROW(201, "201 Created", 0, NGX_HTTP_STATUS_RFC(3, 2)),
    NGX_HTTP_STATUS_ROW(202, "202 Accepted", 0, NGX_HTTP_STATUS_RFC(3, 3)),
    NGX_HTTP_STATUS_GAP(203, NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(3, 4)),
    NGX_HTTP_STATUS_ROW(204, "204 No Content", NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(3, 5)),
    NGX_HTTP_STATUS_GAP(205, 0, NGX_HTTP_STATUS_RFC(3, 6)),
    NGX_HTTP_STATUS_ROW(206, "206 Partial Content", NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(3, 7)),

    /* 3xx redirection (RFC 9110 15.4) */
    NGX_HTTP_STATUS_GAP(300, NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(4, 1)),
    NGX_HTTP_STATUS_ROW(301, "301 Moved Permanently",
        NGX_HTTP_STATUS_CACHEABLE, NGX_HTTP_STATUS_RFC(4, 2)),
    NGX_HTTP_STATUS_ROW(302, "302 Moved Temporarily", 0,
        NGX_HTTP_STATUS_RFC(4, 3)),
    NGX_HTTP_STATUS_ROW(303, "303 See Other", 0, NGX_HTTP_STATUS_RFC(4, 4)),
    NGX_HTTP_STATUS_ROW(304, "304 Not Modified", 0, NGX_HTTP_STATUS_RFC(4, 5)),
    NGX_HTTP_STATUS_GAP(305, 0, NGX_HTTP_STATUS_RFC(4, 6)),
    NGX_HTTP_STATUS_GAP(306, 0, NGX_HTTP_STATUS_RFC(4, 7)),
    NGX_HTTP_STATUS_ROW(307, "307 Temporary Redirect", 0,
        NGX_HTTP_STATUS_RFC(4, 8)),
    NGX_HTTP_STATUS_ROW(308, "308 Permanent Redirect",
        NGX_HTTP_STATUS_CACHEABLE, NGX_HTTP_STATUS_RFC(4, 9)),

    /* 4xx client error (RFC 9110 15.5) */
    NGX_HTTP_STATUS_ROW(400, "400 Bad Request", NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 1)),
    NGX_HTTP_STATUS_ROW(401, "401 Unauthorized", NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 2)),
    NGX_HTTP_STATUS_ROW(402, "402 Payment Required",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 3)),
    NGX_HTTP_STATUS_ROW(403, "403 Forbidden", NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 4)),
    NGX_HTTP_STATUS_ROW(404, "404 Not Found",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(5, 5)),
    NGX_HTTP_STATUS_ROW(405, "405 Not Allowed",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(5, 6)),
    NGX_HTTP_STATUS_ROW(406, "406 Not Acceptable",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 7)),
    NGX_HTTP_STATUS_GAP(407, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 8)),
    NGX_HTTP_STATUS_ROW(408, "408 Request Time-out",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 9)),
    NGX_HTTP_STATUS_ROW(409, "409 Conflict", NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 10)),
    NGX_HTTP_STATUS_ROW(410, "410 Gone",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(5, 11)),
    NGX_HTTP_STATUS_ROW(411, "411 Length Required",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 12)),
    NGX_HTTP_STATUS_ROW(412, "412 Precondition Failed",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 13)),
    NGX_HTTP_STATUS_ROW(413, "413 Request Entity Too Large",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 14)),
    NGX_HTTP_STATUS_ROW(414, "414 Request-URI Too Large",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(5, 15)),
    NGX_HTTP_STATUS_ROW(415, "415 Unsupported Media Type",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 16)),
    NGX_HTTP_STATUS_ROW(416, "416 Requested Range Not Satisfiable",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 17)),
    NGX_HTTP_STATUS_GAP(417, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 18)),
    NGX_HTTP_STATUS_GAP(418, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 19)),
    NGX_HTTP_STATUS_GAP(419, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_GAP(420, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_ROW(421, "421 Misdirected Request",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC(5, 20)),
    NGX_HTTP_STATUS_GAP(422, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 21)),
    NGX_HTTP_STATUS_GAP(423, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_GAP(424, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_GAP(425, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_GAP(426, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC(5, 22)),
    NGX_HTTP_STATUS_GAP(427, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_GAP(428, NGX_HTTP_STATUS_CLIENT_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_ROW(429, "429 Too Many Requests",
        NGX_HTTP_STATUS_CLIENT_ERROR, NGX_HTTP_STATUS_RFC_NONE),

    /* 5xx server error (RFC 9110 15.6) */
    NGX_HTTP_STATUS_ROW(500, "500 Internal Server Error",
        NGX_HTTP_STATUS_SERVER_ERROR, NGX_HTTP_STATUS_RFC(6, 1)),
    NGX_HTTP_STATUS_ROW(501, "501 Not Implemented",
        NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE,
        NGX_HTTP_STATUS_RFC(6, 2)),
    NGX_HTTP_STATUS_ROW(502, "502 Bad Gateway", NGX_HTTP_STATUS_SERVER_ERROR,
        NGX_HTTP_STATUS_RFC(6, 3)),
    NGX_HTTP_STATUS_ROW(503, "503 Service Temporarily Unavailable",
        NGX_HTTP_STATUS_SERVER_ERROR, NGX_HTTP_STATUS_RFC(6, 4)),
    NGX_HTTP_STATUS_ROW(504, "504 Gateway Time-out",
        NGX_HTTP_STATUS_SERVER_ERROR, NGX_HTTP_STATUS_RFC(6, 5)),
    NGX_HTTP_STATUS_ROW(505, "505 HTTP Version Not Supported",
        NGX_HTTP_STATUS_SERVER_ERROR, NGX_HTTP_STATUS_RFC(6, 6)),
    NGX_HTTP_STATUS_GAP(506, NGX_HTTP_STATUS_SERVER_ERROR,
        NGX_HTTP_STATUS_RFC_NONE),
    NGX_HTTP_STATUS_ROW(507, "507 Insufficient Storage",
        NGX_HTTP_STATUS_SERVER_ERROR, NGX_HTTP_STATUS_RFC_NONE)
};


/* Per-worker status-class counters (see ngx_http_status.h). */

ngx_atomic_t  ngx_http_status_counters[NGX_HTTP_STATUS_CLASSES];


/*
 * Translate a status code to its registry slot: one class comparison plus one
 * subtraction, a direct O(1) index with no search, hashing, or caching.  Codes
 * outside the seeded classes yield NULL.
 */

static const ngx_http_status_def_t *
ngx_http_status_lookup(ngx_uint_t status)
{
    ngx_uint_t  index;

    if (status >= NGX_HTTP_CONTINUE && status < NGX_HTTP_STATUS_LAST_1XX) {
        index = status - NGX_HTTP_CONTINUE;

    } else if (status >= NGX_HTTP_OK && status < NGX_HTTP_STATUS_LAST_2XX) {
        index = status - NGX_HTTP_OK + NGX_HTTP_STATUS_OFF_2XX;

    } else if (status >= NGX_HTTP_SPECIAL_RESPONSE
               && status < NGX_HTTP_STATUS_LAST_3XX)
    {
        index = status - NGX_HTTP_SPECIAL_RESPONSE + NGX_HTTP_STATUS_OFF_3XX;

    } else if (status >= NGX_HTTP_BAD_REQUEST
               && status < NGX_HTTP_STATUS_LAST_4XX)
    {
        index = status - NGX_HTTP_BAD_REQUEST + NGX_HTTP_STATUS_OFF_4XX;

    } else if (status >= NGX_HTTP_INTERNAL_SERVER_ERROR
               && status < NGX_HTTP_STATUS_LAST_5XX)
    {
        index = status - NGX_HTTP_INTERNAL_SERVER_ERROR
                + NGX_HTTP_STATUS_OFF_5XX;

    } else {
        return NULL;
    }

    return &ngx_http_status_defs[index];
}


/*
 * Return the full wire status line for a status code, e.g. "200 OK" for 200.
 * This is the hot-path source: the HTTP/1.x header filter emits the returned
 * bytes with a single copy and no per-response formatting.  Gap rows and
 * unknown codes have no line and yield an empty ngx_str_t, so the caller
 * renders the numeric status only.
 */

ngx_str_t
ngx_http_status_line(ngx_uint_t status)
{
    const ngx_http_status_def_t  *def;

    ngx_str_t  line;

    def = ngx_http_status_lookup(status);

    if (def != NULL && def->line != NULL) {
        line.len = def->line_len;
        line.data = (u_char *) def->line;
        return line;
    }

    ngx_str_null(&line);

    return line;
}


/*
 * Return the bare wire reason phrase for a status code, e.g. "OK" for 200.
 * The registry stores the full status line ("200 OK"), so the phrase is the
 * line past its invariant four-character "NNN " code prefix; every registry
 * code is three digits, so the prefix is always exactly four bytes.  Gap rows
 * and unknown codes have no phrase and yield an empty ngx_str_t.
 */

ngx_str_t
ngx_http_status_reason(ngx_uint_t status)
{
    const ngx_http_status_def_t  *def;

    ngx_str_t  reason;

    def = ngx_http_status_lookup(status);

    if (def != NULL && def->line != NULL) {
        reason.len = def->line_len - (sizeof("000 ") - 1);
        reason.data = (u_char *) def->line + (sizeof("000 ") - 1);
        return reason;
    }

    ngx_str_null(&reason);

    return reason;
}


/*
 * Report whether a status code is heuristically cacheable per RFC 9110 15.1.
 * Unknown or out-of-range codes are treated as not cacheable.
 */

ngx_uint_t
ngx_http_status_is_cacheable(ngx_uint_t status)
{
    const ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def != NULL && (def->flags & NGX_HTTP_STATUS_CACHEABLE)) {
        return 1;
    }

    return 0;
}


/*
 * Validate a status code against RFC 9110.  The range check is compiled in
 * only when validation is enabled at configure time; the default build folds
 * this to a constant NGX_OK so the write path carries no extra cost.
 */

ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
#if (NGX_HTTP_STATUS_VALIDATION)

    if (status < 100 || status > 599) {
        return NGX_ERROR;
    }

    return NGX_OK;

#else

    (void) status;

    return NGX_OK;

#endif
}


/*
 * Increment the status-class counter for a valid code; ignore out-of-range.
 * The counters back the validation-only stub_status metric lines, so the body
 * is compiled in only when validation is enabled.  In the default build this
 * is a no-op, so the write path carries no extra atomic and the stub_status
 * output stays byte-identical to historical nginx.
 */

static ngx_inline void
ngx_http_status_count(ngx_uint_t status)
{
#if (NGX_HTTP_STATUS_VALIDATION)

    ngx_uint_t  slot;

    if (status < 100 || status > 599) {
        return;
    }

    slot = status / 100 - 1;

    (void) ngx_atomic_fetch_add(&ngx_http_status_counters[slot], 1);

#else

    (void) status;

#endif
}


/*
 * Index of the $request_id variable, resolved once during configuration so
 * that ngx_http_status_log() reads the request's cached id instead of
 * regenerating a fresh value on every call.  It remains NGX_ERROR until
 * ngx_http_status_log_init() runs, in which case the logger falls back to a
 * by-name lookup.
 */

static ngx_int_t  ngx_http_status_reqid_index = NGX_ERROR;


/*
 * Reserve an index for $request_id during the configuration phase, before the
 * worker fork.  When $request_id is also referenced elsewhere (for example in
 * a log_format) the index subsystem returns the shared slot, so the id logged
 * here matches the access-log id exactly for the same request.
 */

void
ngx_http_status_log_init(ngx_conf_t *cf)
{
    ngx_str_t  name = ngx_string("request_id");

    ngx_http_status_reqid_index = ngx_http_get_variable_index(cf, &name);
}


/*
 * Emit a status-API log event correlated with the request's $request_id.  The
 * request and its connection are checked so the helper is safe on teardown
 * paths; an unresolved id is logged as an empty string.
 */

void
ngx_http_status_log(ngx_http_request_t *r, ngx_uint_t level,
    const char *action, ngx_uint_t status)
{
    ngx_str_t                   request_id;
    ngx_http_variable_value_t  *vv;

    static ngx_str_t  name = ngx_string("request_id");

    if (r == NULL || r->connection == NULL) {
        return;
    }

    ngx_str_null(&request_id);

    if (ngx_http_status_reqid_index != NGX_ERROR) {
        vv = ngx_http_get_indexed_variable(r, ngx_http_status_reqid_index);

    } else {
        vv = ngx_http_get_variable(r, &name, ngx_hash_key(name.data, name.len));
    }

    if (vv != NULL && vv->valid && !vv->not_found) {
        request_id.len = vv->len;
        request_id.data = vv->data;
    }

    ngx_log_error(level, r->connection->log, 0,
                  "%s: %ui, request_id: \"%V\"", action, status, &request_id);
}


#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * Strict-mode RFC 9110 15.2 interaction checks against the prior value of
 * r->headers_out.status (no per-request storage is added): a 1xx must precede
 * the final status, and only one final status may be sent.  Violations are
 * logged, never rejected, so internal status transitions are preserved.
 */

static void
ngx_http_status_check(ngx_http_request_t *r, ngx_uint_t status)
{
    ngx_uint_t                    prev;
    const ngx_http_status_def_t  *def;

    prev = r->headers_out.status;

    if (status < NGX_HTTP_OK) {
        if (prev >= NGX_HTTP_OK) {

            /*
             * A 1xx must precede the final status (RFC 9110 15.2).  Identify
             * the offending final status by its RFC 9110 section, decoded from
             * the registry row's packed rfc_section field; an unregistered
             * final status decodes to section 0.0.
             */

            def = ngx_http_status_lookup(prev);

            ngx_log_error(NGX_HTTP_STATUS_LOG_LEVEL, r->connection->log, 0,
                          "http informational status %ui sent after final "
                          "status %ui (RFC 9110 15.%ui.%ui)", status, prev,
                          def ? (ngx_uint_t) (def->rfc_section >> 8)
                              : (ngx_uint_t) 0,
                          def ? (ngx_uint_t) (def->rfc_section & 0xff)
                              : (ngx_uint_t) 0);

            ngx_http_status_log(r, NGX_HTTP_STATUS_LOG_LEVEL,
                "informational status after final status", status);
        }

    } else if (prev >= NGX_HTTP_OK && prev != status) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http status final %ui replaces %ui", status, prev);
    }
}

#endif


/*
 * The single sanctioned write path for r->headers_out.status.  Upstream-origin
 * codes pass through unchanged and unvalidated so non-standard backend codes
 * are never rejected or transformed.  Locally generated codes are validated
 * first (a no-op in the default build) and rejected codes are counted, logged,
 * and reported via NGX_ERROR.
 */

ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    if (r == NULL) {
        return NGX_ERROR;
    }

    if (r->upstream != NULL) {
        r->headers_out.status = status;
        ngx_http_status_count(status);

#if (NGX_HTTP_STATUS_VALIDATION)
        if (status < 100 || status > 599) {
            ngx_http_status_log(r, NGX_HTTP_STATUS_LOG_LEVEL,
                                "upstream sent non-standard status", status);
        }
#endif

        return NGX_OK;
    }

#if !(NGX_HTTP_STATUS_VALIDATION) && (NGX_DEBUG)
    /*
     * Standard-mode diagnostics: with strict validation compiled out, surface
     * locally generated codes outside the 100..599 range at debug level only.
     * This block is absent from non-debug and validation builds, so the write
     * path carries no extra cost there.
     */
    if (status < 100 || status > 599) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http suspicious status %ui set (standard mode)",
                       status);
    }
#endif

    if (ngx_http_status_validate(status) != NGX_OK) {
        ngx_http_status_log(r, NGX_HTTP_STATUS_LOG_LEVEL,
                            "invalid HTTP status rejected", status);
        (void) ngx_atomic_fetch_add(
                   &ngx_http_status_counters[NGX_HTTP_STATUS_REJECTED], 1);
        return NGX_ERROR;
    }

#if (NGX_HTTP_STATUS_VALIDATION)
    ngx_http_status_check(r, status);
#endif

    r->headers_out.status = status;
    ngx_http_status_count(status);

    return NGX_OK;
}


/*
 * Finalize the registry during the configuration phase, before any worker is
 * forked.  The table is a compile-time constant, so nothing is allocated; this
 * only verifies every row is reachable at the direct index its code maps to,
 * guarding against a future mis-ordered edit.  It is safe to call repeatedly.
 */

ngx_int_t
ngx_http_status_register(void)
{
    ngx_uint_t  i, n;

    n = sizeof(ngx_http_status_defs) / sizeof(ngx_http_status_defs[0]);

    for (i = 0; i < n; i++) {
        if (ngx_http_status_lookup(ngx_http_status_defs[i].code)
            != &ngx_http_status_defs[i])
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}
