
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Class boundary and offset constants for the direct-index registry.  They
 * mirror the class-offset scheme historically used by the header filter's
 * ngx_http_status_lines[] table so that the registry is a byte-for-byte
 * superset of that table.  Each LAST_* value is the exclusive upper bound of
 * its class; each OFF_* value is the index at which the next class begins.
 */

#define NGX_HTTP_STATUS_LAST_2XX   207
#define NGX_HTTP_STATUS_OFF_3XX    (NGX_HTTP_STATUS_LAST_2XX - 200)

#define NGX_HTTP_STATUS_LAST_3XX   309
#define NGX_HTTP_STATUS_OFF_4XX    (NGX_HTTP_STATUS_LAST_3XX - 301             \
                                    + NGX_HTTP_STATUS_OFF_3XX)

#define NGX_HTTP_STATUS_LAST_4XX   430
#define NGX_HTTP_STATUS_OFF_5XX    (NGX_HTTP_STATUS_LAST_4XX - 400             \
                                    + NGX_HTTP_STATUS_OFF_4XX)

#define NGX_HTTP_STATUS_LAST_5XX   508


/*
 * Centralized HTTP status registry.  The table is a compile-time constant and
 * therefore lives in the read-only data segment: it is fully populated before
 * the first worker fork and shared copy-on-write across every worker, so its
 * per-worker cost is negligible and no locking or thread-local storage is
 * required.  Rows are laid out contiguously by class offset; gap rows keep the
 * direct-index arithmetic exact and carry ngx_null_string so that the reason
 * lookup falls back to the numeric status, matching the previously shipped
 * wire output exactly.  Each reason phrase is the bare reason token (no
 * numeric code prefix); the header filter writes the numeric status code and a
 * separating space before it, reproducing nginx's historical status lines
 * byte-for-byte, including nginx's historical spellings.
 */

static const ngx_http_status_def_t  ngx_http_status_defs[] = {

    /* 2xx successful (RFC 9110 15.3) */
    { 200, ngx_string("OK"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC9110#15.3.1" },
    { 201, ngx_string("Created"),
      0, "RFC9110#15.3.2" },
    { 202, ngx_string("Accepted"),
      0, "RFC9110#15.3.3" },
    { 203, ngx_null_string,                      /* Non-Authoritative */
      NGX_HTTP_STATUS_CACHEABLE, NULL },
    { 204, ngx_string("No Content"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC9110#15.3.5" },
    { 205, ngx_null_string,                      /* Reset Content */
      0, NULL },
    { 206, ngx_string("Partial Content"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC9110#15.3.7" },

    /* 3xx redirection (RFC 9110 15.4) */
    { 301, ngx_string("Moved Permanently"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC9110#15.4.2" },
    { 302, ngx_string("Moved Temporarily"),
      0, "RFC9110#15.4.3" },
    { 303, ngx_string("See Other"),
      0, "RFC9110#15.4.4" },
    { 304, ngx_string("Not Modified"),
      0, "RFC9110#15.4.5" },
    { 305, ngx_null_string,                      /* Use Proxy */
      0, NULL },
    { 306, ngx_null_string,                      /* unused */
      0, NULL },
    { 307, ngx_string("Temporary Redirect"),
      0, "RFC9110#15.4.8" },
    { 308, ngx_string("Permanent Redirect"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC9110#15.4.9" },

    /* 4xx client error (RFC 9110 15.5) */
    { 400, ngx_string("Bad Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.1" },
    { 401, ngx_string("Unauthorized"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.2" },
    { 402, ngx_string("Payment Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.3" },
    { 403, ngx_string("Forbidden"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.4" },
    { 404, ngx_string("Not Found"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC9110#15.5.5" },
    { 405, ngx_string("Not Allowed"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC9110#15.5.6" },
    { 406, ngx_string("Not Acceptable"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.7" },
    { 407, ngx_null_string,                      /* Proxy Auth Required */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 408, ngx_string("Request Time-out"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.9" },
    { 409, ngx_string("Conflict"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.10" },
    { 410, ngx_string("Gone"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC9110#15.5.11" },
    { 411, ngx_string("Length Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.12" },
    { 412, ngx_string("Precondition Failed"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.13" },
    { 413, ngx_string("Request Entity Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.14" },
    { 414, ngx_string("Request-URI Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC9110#15.5.15" },
    { 415, ngx_string("Unsupported Media Type"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.16" },
    { 416, ngx_string("Requested Range Not Satisfiable"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.17" },
    { 417, ngx_null_string,                      /* Expectation Failed */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 418, ngx_null_string,                      /* unused */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 419, ngx_null_string,                      /* unused */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 420, ngx_null_string,                      /* unused */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 421, ngx_string("Misdirected Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC9110#15.5.20" },
    { 422, ngx_null_string,                      /* Unprocessable Content */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 423, ngx_null_string,                      /* Locked */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 424, ngx_null_string,                      /* Failed Dependency */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 425, ngx_null_string,                      /* unused */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 426, ngx_null_string,                      /* Upgrade Required */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 427, ngx_null_string,                      /* unused */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 428, ngx_null_string,                      /* Precondition Required */
      NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 429, ngx_string("Too Many Requests"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC6585#4" },

    /* 5xx server error (RFC 9110 15.6) */
    { 500, ngx_string("Internal Server Error"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC9110#15.6.1" },
    { 501, ngx_string("Not Implemented"),
      NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC9110#15.6.2" },
    { 502, ngx_string("Bad Gateway"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC9110#15.6.3" },
    { 503, ngx_string("Service Temporarily Unavailable"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC9110#15.6.4" },
    { 504, ngx_string("Gateway Time-out"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC9110#15.6.5" },
    { 505, ngx_string("HTTP Version Not Supported"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC9110#15.6.6" },
    { 506, ngx_null_string,                      /* Variant Also Negotiates */
      NGX_HTTP_STATUS_SERVER_ERROR, NULL },
    { 507, ngx_string("Insufficient Storage"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC4918#11.5" }
};


/*
 * Translate a status code to its registry slot.  The mapping is a single
 * class comparison plus one subtraction -- a direct O(1) index with no search,
 * no hashing, and no caching.  Codes outside the seeded classes yield NULL.
 */

static const ngx_http_status_def_t *
ngx_http_status_lookup(ngx_uint_t status)
{
    ngx_uint_t  index;

    if (status >= NGX_HTTP_OK && status < NGX_HTTP_STATUS_LAST_2XX) {
        index = status - NGX_HTTP_OK;

    } else if (status >= NGX_HTTP_MOVED_PERMANENTLY
               && status < NGX_HTTP_STATUS_LAST_3XX)
    {
        index = status - NGX_HTTP_MOVED_PERMANENTLY + NGX_HTTP_STATUS_OFF_3XX;

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
 * Return the wire reason phrase for a status code, e.g. "200 OK".  Gap rows
 * and codes outside the seeded classes have no phrase and yield an empty
 * ngx_str_t, so the caller renders the numeric status -- byte-identical to the
 * output produced before the registry was introduced.
 */

ngx_str_t
ngx_http_status_reason(ngx_uint_t status)
{
    const ngx_http_status_def_t  *def;

    ngx_str_t  none = ngx_null_string;

    def = ngx_http_status_lookup(status);

    if (def != NULL && def->reason.len != 0) {
        return def->reason;
    }

    return none;
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
 * The single sanctioned write path for r->headers_out.status.  Upstream-origin
 * codes always pass through unchanged and unvalidated so that non-standard
 * backend codes are never rejected or transformed.  For locally generated
 * responses the code is validated first; in the default build validation is a
 * no-op, so the field is always written and NGX_OK returned.
 */

ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    if (r->upstream != NULL) {
        r->headers_out.status = status;

#if (NGX_HTTP_STATUS_VALIDATION)
        if (status < 100 || status > 599) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                          "upstream sent non-standard status %ui", status);
        }
#endif

        return NGX_OK;
    }

    if (ngx_http_status_validate(status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "invalid HTTP status %ui rejected", status);
        return NGX_ERROR;
    }

    r->headers_out.status = status;

    return NGX_OK;
}


/*
 * Finalize the registry during the configuration phase, before any worker is
 * forked.  The table is a compile-time constant, so nothing is allocated or
 * mutated; this only verifies that every row is reachable at the direct index
 * its code maps to, guarding against a future mis-ordered edit.  It is safe to
 * call more than once.
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

