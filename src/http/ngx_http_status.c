
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
 * the index at which the next class begins.  The 3xx class starts at 300
 * (NGX_HTTP_SPECIAL_RESPONSE) so that code has its own row.
 */

#define NGX_HTTP_STATUS_LAST_2XX   207
#define NGX_HTTP_STATUS_OFF_3XX    (NGX_HTTP_STATUS_LAST_2XX - NGX_HTTP_OK)

#define NGX_HTTP_STATUS_LAST_3XX   309
#define NGX_HTTP_STATUS_OFF_4XX    (NGX_HTTP_STATUS_LAST_3XX                   \
                                    - NGX_HTTP_SPECIAL_RESPONSE                \
                                    + NGX_HTTP_STATUS_OFF_3XX)

#define NGX_HTTP_STATUS_LAST_4XX   430
#define NGX_HTTP_STATUS_OFF_5XX    (NGX_HTTP_STATUS_LAST_4XX                   \
                                    - NGX_HTTP_BAD_REQUEST                     \
                                    + NGX_HTTP_STATUS_OFF_4XX)

#define NGX_HTTP_STATUS_LAST_5XX   508


/* Registry row constructors: a known reason phrase, or a numeric-only gap. */

#define NGX_HTTP_STATUS_ROW(code, reason, flags)                              \
    { (reason), (uint16_t) (sizeof(reason) - 1), (code), (flags) }
#define NGX_HTTP_STATUS_GAP(code, flags)                                      \
    { NULL, 0, (code), (flags) }


/*
 * Centralized HTTP status registry: a compile-time-constant, read-only table
 * laid out contiguously by class offset and indexed in O(1).  Populated before
 * the first worker fork and shared copy-on-write, it needs no locking.  Gap
 * rows carry no reason phrase, so their codes render numeric-only, reproducing
 * nginx's historical status lines byte-for-byte.
 */

static const ngx_http_status_def_t  ngx_http_status_defs[] = {

    /* 2xx successful */
    NGX_HTTP_STATUS_ROW(200, "OK", NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(201, "Created", 0),
    NGX_HTTP_STATUS_ROW(202, "Accepted", 0),
    NGX_HTTP_STATUS_GAP(203, NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(204, "No Content", NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_GAP(205, 0),
    NGX_HTTP_STATUS_ROW(206, "Partial Content", NGX_HTTP_STATUS_CACHEABLE),

    /* 3xx redirection */
    NGX_HTTP_STATUS_GAP(300, NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(301, "Moved Permanently", NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(302, "Moved Temporarily", 0),
    NGX_HTTP_STATUS_ROW(303, "See Other", 0),
    NGX_HTTP_STATUS_ROW(304, "Not Modified", 0),
    NGX_HTTP_STATUS_GAP(305, 0),
    NGX_HTTP_STATUS_GAP(306, 0),
    NGX_HTTP_STATUS_ROW(307, "Temporary Redirect", 0),
    NGX_HTTP_STATUS_ROW(308, "Permanent Redirect", NGX_HTTP_STATUS_CACHEABLE),

    /* 4xx client error */
    NGX_HTTP_STATUS_ROW(400, "Bad Request", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(401, "Unauthorized", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(402, "Payment Required", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(403, "Forbidden", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(404, "Not Found",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(405, "Not Allowed",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(406, "Not Acceptable", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(407, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(408, "Request Time-out", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(409, "Conflict", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(410, "Gone",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(411, "Length Required", NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(412, "Precondition Failed",
        NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(413, "Request Entity Too Large",
        NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(414, "Request-URI Too Large",
        NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(415, "Unsupported Media Type",
        NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(416, "Requested Range Not Satisfiable",
        NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(417, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(418, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(419, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(420, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(421, "Misdirected Request",
        NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(422, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(423, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(424, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(425, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(426, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(427, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_GAP(428, NGX_HTTP_STATUS_CLIENT_ERROR),
    NGX_HTTP_STATUS_ROW(429, "Too Many Requests", NGX_HTTP_STATUS_CLIENT_ERROR),

    /* 5xx server error */
    NGX_HTTP_STATUS_ROW(500, "Internal Server Error",
        NGX_HTTP_STATUS_SERVER_ERROR),
    NGX_HTTP_STATUS_ROW(501, "Not Implemented",
        NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE),
    NGX_HTTP_STATUS_ROW(502, "Bad Gateway", NGX_HTTP_STATUS_SERVER_ERROR),
    NGX_HTTP_STATUS_ROW(503, "Service Temporarily Unavailable",
        NGX_HTTP_STATUS_SERVER_ERROR),
    NGX_HTTP_STATUS_ROW(504, "Gateway Time-out", NGX_HTTP_STATUS_SERVER_ERROR),
    NGX_HTTP_STATUS_ROW(505, "HTTP Version Not Supported",
        NGX_HTTP_STATUS_SERVER_ERROR),
    NGX_HTTP_STATUS_GAP(506, NGX_HTTP_STATUS_SERVER_ERROR),
    NGX_HTTP_STATUS_ROW(507, "Insufficient Storage",
        NGX_HTTP_STATUS_SERVER_ERROR)
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

    if (status >= NGX_HTTP_OK && status < NGX_HTTP_STATUS_LAST_2XX) {
        index = status - NGX_HTTP_OK;

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
 * Return the wire reason phrase for a status code, e.g. "OK" for 200.  Gap
 * rows and unknown codes have no phrase and yield an empty ngx_str_t, so the
 * caller renders the numeric status.
 */

ngx_str_t
ngx_http_status_reason(ngx_uint_t status)
{
    const ngx_http_status_def_t  *def;

    ngx_str_t  reason;

    def = ngx_http_status_lookup(status);

    if (def != NULL && def->reason != NULL) {
        reason.len = def->reason_len;
        reason.data = (u_char *) def->reason;
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


/* Increment the status-class counter for a valid code; ignore out-of-range. */

static ngx_inline void
ngx_http_status_count(ngx_uint_t status)
{
    ngx_uint_t  slot;

    if (status < 100 || status > 599) {
        return;
    }

    slot = status / 100 - 1;

    (void) ngx_atomic_fetch_add(&ngx_http_status_counters[slot], 1);
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
    ngx_uint_t  prev;

    prev = r->headers_out.status;

    if (status < NGX_HTTP_OK) {
        if (prev >= NGX_HTTP_OK) {
            ngx_http_status_log(r, NGX_LOG_WARN,
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
            ngx_http_status_log(r, NGX_LOG_WARN,
                                "upstream sent non-standard status", status);
        }
#endif

        return NGX_OK;
    }

    if (ngx_http_status_validate(status) != NGX_OK) {
        ngx_http_status_log(r, NGX_LOG_ERR, "invalid HTTP status rejected",
                            status);
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
