
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Single source of truth for HTTP status-code metadata.  The registry maps a
 * status code to its reason phrase (rendered verbatim on the status line), its
 * RFC 9110 class plus cacheability flags, and the RFC section that defines it.
 * ngx_http_status_reason() replaces the per-module reason-phrase tables and the
 * divergent offset arithmetic; the reason strings below are byte-identical to
 * the legacy ngx_http_status_lines[] so the wire output is unchanged.
 */


/*
 * Maximum number of third-party status codes that may be added through
 * ngx_http_status_register() before worker fork.  The registration table is
 * treated as read-only once the workers have started.
 */
#define NGX_HTTP_STATUS_MAX_REGISTERED  32


/*
 * Core registry.  Entries are stored in ascending code order grouped by class
 * so that ngx_http_status_index() resolves a code to its slot with O(1)
 * arithmetic; the offsets in that helper MUST stay in lock-step with the order
 * of this array.  The array is "static const": it lives in the read-only data
 * segment, is shared across all workers, and is never modified at runtime, so
 * the per-worker incremental footprint is effectively zero.
 *
 * Codes that historically emitted a numeric-only status line (all 1xx, every
 * nginx extension code, and the gaps within the 2xx-5xx ranges) carry an empty
 * reason (ngx_null_string) so that behavior is preserved byte-for-byte.
 */
static const ngx_http_status_def_t  ngx_http_status_defs[] = {

    /* 1xx informational - reason empty (legacy emitted numeric-only) */
    { 100, ngx_null_string, NGX_HTTP_STATUS_INFORMATIONAL, "15.2.1" },
    { 101, ngx_null_string, NGX_HTTP_STATUS_INFORMATIONAL, "15.2.2" },
    { 102, ngx_null_string, NGX_HTTP_STATUS_INFORMATIONAL, NULL },
    { 103, ngx_null_string, NGX_HTTP_STATUS_INFORMATIONAL, NULL },

    /* 2xx */
    { 200, ngx_string("200 OK"),               NGX_HTTP_STATUS_CACHEABLE, "15.3.1" },
    { 201, ngx_string("201 Created"),          0, "15.3.2" },
    { 202, ngx_string("202 Accepted"),         0, "15.3.3" },
    { 203, ngx_null_string,                    NGX_HTTP_STATUS_CACHEABLE, "15.3.4" },
    { 204, ngx_string("204 No Content"),       NGX_HTTP_STATUS_CACHEABLE, "15.3.5" },
    { 205, ngx_null_string,                    0, "15.3.6" },
    { 206, ngx_string("206 Partial Content"),  NGX_HTTP_STATUS_CACHEABLE, "15.3.7" },

    /* 3xx */
    { 300, ngx_null_string,                       NGX_HTTP_STATUS_CACHEABLE, "15.4.1" },
    { 301, ngx_string("301 Moved Permanently"),   NGX_HTTP_STATUS_CACHEABLE, "15.4.2" },
    { 302, ngx_string("302 Moved Temporarily"),   0, "15.4.3" },
    { 303, ngx_string("303 See Other"),           0, "15.4.4" },
    { 304, ngx_string("304 Not Modified"),        0, "15.4.5" },
    { 305, ngx_null_string,                       0, "15.4.6" },
    { 306, ngx_null_string,                       0, NULL },
    { 307, ngx_string("307 Temporary Redirect"),  0, "15.4.8" },
    { 308, ngx_string("308 Permanent Redirect"),  NGX_HTTP_STATUS_CACHEABLE, "15.4.9" },

    /* 4xx - contiguous 400..429 (gaps carry empty reason for O(1) indexing) */
    { 400, ngx_string("400 Bad Request"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.1" },
    { 401, ngx_string("401 Unauthorized"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.2" },
    { 402, ngx_string("402 Payment Required"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.3" },
    { 403, ngx_string("403 Forbidden"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.4" },
    { 404, ngx_string("404 Not Found"),
           NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.5" },
    { 405, ngx_string("405 Not Allowed"),
           NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.6" },
    { 406, ngx_string("406 Not Acceptable"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.7" },
    { 407, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.8" },
    { 408, ngx_string("408 Request Time-out"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.9" },
    { 409, ngx_string("409 Conflict"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.10" },
    { 410, ngx_string("410 Gone"),
           NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.11" },
    { 411, ngx_string("411 Length Required"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.12" },
    { 412, ngx_string("412 Precondition Failed"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.13" },
    { 413, ngx_string("413 Request Entity Too Large"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.14" },
    { 414, ngx_string("414 Request-URI Too Large"),
           NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.15" },
    { 415, ngx_string("415 Unsupported Media Type"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.16" },
    { 416, ngx_string("416 Requested Range Not Satisfiable"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.17" },
    { 417, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.18" },
    { 418, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 419, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 420, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 421, ngx_string("421 Misdirected Request"),
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.20" },
    { 422, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.21" },
    { 423, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 424, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 425, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 426, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.22" },
    { 427, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 428, ngx_null_string,
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 429, ngx_string("429 Too Many Requests"),
           NGX_HTTP_STATUS_CLIENT_ERROR, NULL },

    /* 5xx */
    { 500, ngx_string("500 Internal Server Error"),
           NGX_HTTP_STATUS_SERVER_ERROR, "15.6.1" },
    { 501, ngx_string("501 Not Implemented"),
           NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.6.2" },
    { 502, ngx_string("502 Bad Gateway"),
           NGX_HTTP_STATUS_SERVER_ERROR, "15.6.3" },
    { 503, ngx_string("503 Service Temporarily Unavailable"),
           NGX_HTTP_STATUS_SERVER_ERROR, "15.6.4" },
    { 504, ngx_string("504 Gateway Time-out"),
           NGX_HTTP_STATUS_SERVER_ERROR, "15.6.5" },
    { 505, ngx_string("505 HTTP Version Not Supported"),
           NGX_HTTP_STATUS_SERVER_ERROR, "15.6.6" },
    { 506, ngx_null_string,
           NGX_HTTP_STATUS_SERVER_ERROR, NULL },
    { 507, ngx_string("507 Insufficient Storage"),
           NGX_HTTP_STATUS_SERVER_ERROR, NULL },

    /* nginx extension codes - reason empty; preserved behaviors live in callers */
    { 444, ngx_null_string, 0, NULL },                           /* NGX_HTTP_CLOSE */
    { 494, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* REQUEST_HEADER_TOO_LARGE */
    { 495, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* HTTPS_CERT_ERROR */
    { 496, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* HTTPS_NO_CERT */
    { 497, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* TO_HTTPS */
    { 498, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* canceled (invalid host) */
    { 499, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }  /* CLIENT_CLOSED_REQUEST */
};


/*
 * Table of third-party codes added through ngx_http_status_register().  It is
 * populated only before worker fork and never modified afterwards, so each
 * worker inherits an identical, effectively read-only copy.
 */
static ngx_http_status_def_t  ngx_http_status_registered[NGX_HTTP_STATUS_MAX_REGISTERED];
static ngx_uint_t             ngx_http_status_nregistered = 0;


/*
 * Initialization/freeze flag shared by ngx_http_status_init() and
 * ngx_http_status_register().  ngx_http_status_init() sets it to 1 from a
 * pre-fork HTTP postconfiguration hook; once set, the registration table is
 * frozen and ngx_http_status_register() refuses further additions, so every
 * worker forked afterwards inherits an identical, read-only registry.
 */
static ngx_uint_t             ngx_http_status_initialized = 0;


/*
 * Resolve a status code to its slot in ngx_http_status_defs[] using O(1)
 * class-offset arithmetic, or return -1 when the code is not part of the core
 * registry.  The offsets below MUST match the order of the array above.
 */
static ngx_int_t
ngx_http_status_index(ngx_uint_t status)
{
    if (status >= 100 && status <= 103) {
        return (ngx_int_t) (status - 100);          /* 0..3   */
    }

    if (status >= 200 && status <= 206) {
        return (ngx_int_t) (status - 200 + 4);      /* 4..10  */
    }

    if (status >= 300 && status <= 308) {
        return (ngx_int_t) (status - 300 + 11);     /* 11..19 */
    }

    if (status >= 400 && status <= 429) {
        return (ngx_int_t) (status - 400 + 20);     /* 20..49 */
    }

    if (status >= 500 && status <= 507) {
        return (ngx_int_t) (status - 500 + 50);     /* 50..57 */
    }

    if (status == 444) {
        return 58;
    }

    if (status >= 494 && status <= 499) {
        return (ngx_int_t) (status - 494 + 59);     /* 59..64 */
    }

    return -1;
}


/*
 * Return the registry descriptor for a status code, or NULL if it is unknown.
 * The core array is consulted first (O(1)); codes outside the core ranges fall
 * back to a linear scan of the small, bounded registration table.
 */
static const ngx_http_status_def_t *
ngx_http_status_lookup(ngx_uint_t status)
{
    ngx_int_t   i;
    ngx_uint_t  n;

    i = ngx_http_status_index(status);

    if (i >= 0) {
        return &ngx_http_status_defs[i];
    }

    for (n = 0; n < ngx_http_status_nregistered; n++) {
        if (ngx_http_status_registered[n].code == status) {
            return &ngx_http_status_registered[n];
        }
    }

    return NULL;
}


/*
 * Return the reason phrase for a status code.  The data of a non-empty result
 * points into static storage that lives for the lifetime of the process, so
 * returning the value by copy is safe.  Numeric-only codes and unknown codes
 * yield an empty string, which callers render as a bare numeric status line.
 */
ngx_str_t
ngx_http_status_reason(ngx_uint_t status)
{
    ngx_str_t                     reason;
    const ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def != NULL) {
        return def->reason;
    }

    ngx_str_null(&reason);

    return reason;
}


/*
 * RFC 9110 structural conformance check: a status code is well-formed when it
 * lies in the three-digit class range 100..599.  This accepts every standard
 * code and the nginx extension codes (444, 494..499).  Codes that are merely
 * unknown but in range are still structurally valid and are handled
 * permissively by ngx_http_status_set().
 */
ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
    if (status >= 100 && status <= 599) {
        return NGX_OK;
    }

    return NGX_ERROR;
}


/*
 * Return the registry flag bitmask (class + cacheability) for a status code,
 * or 0 when the code is unknown to the registry.  This is the single accessor
 * for the registry's authoritative class metadata; ngx_http_status_is_cacheable()
 * and the error-page renderer both read their class decisions from it so that
 * no consumer re-derives the code-space structure on its own.
 */
ngx_uint_t
ngx_http_status_flags(ngx_uint_t status)
{
    const ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def != NULL) {
        return def->flags;
    }

    return 0;
}


/*
 * Return 1 when the code is known to the registry and is flagged cacheable by
 * default, 0 otherwise.  This exposes metadata only and introduces no caching
 * behavior of its own; nginx's cache layer is unaffected.
 */
ngx_uint_t
ngx_http_status_is_cacheable(ngx_uint_t status)
{
    if (ngx_http_status_flags(status) & NGX_HTTP_STATUS_CACHEABLE) {
        return 1;
    }

    return 0;
}


/*
 * Register a third-party status code before worker fork.  Registration is
 * rejected with NGX_ERROR for a NULL descriptor, once the registry has been
 * frozen by ngx_http_status_init() (the table is read-only after pre-fork
 * initialization), or on overflow of the fixed-capacity table; otherwise the
 * descriptor is copied into the table, where ngx_http_status_lookup() finds it.
 */
ngx_int_t
ngx_http_status_register(const ngx_http_status_def_t *def)
{
    if (def == NULL) {
        return NGX_ERROR;
    }

    if (ngx_http_status_initialized) {
        /* the registry is frozen after pre-fork initialization */
        return NGX_ERROR;
    }

    if (ngx_http_status_nregistered >= NGX_HTTP_STATUS_MAX_REGISTERED) {
        return NGX_ERROR;
    }

    ngx_http_status_registered[ngx_http_status_nregistered] = *def;
    ngx_http_status_nregistered++;

    return NGX_OK;
}


#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * The single status-assignment chokepoint (strict-validation build).  Every
 * outcome is recorded with a request-correlated NGX_LOG_DEBUG_HTTP line: the
 * connection number (%uA) is the correlation key and the connection log already
 * carries nginx's per-request context (client, request line, $request_id when
 * logged), so each event is traceable to its request.  A proxied response is
 * passed through verbatim so that an upstream's status is never rejected.  A
 * locally generated out-of-range code is refused; the rejection is logged here
 * at debug level so that even permissive callers that ignore the return value
 * stay observable, while the standard-pattern callers emit the NGX_LOG_ERR and
 * fall back to NGX_HTTP_INTERNAL_SERVER_ERROR.  Logging at error level here is
 * deliberately avoided so it does not duplicate those callers' messages.
 */
ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    if (r->upstream != NULL) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http status set %ui c:%uA (upstream pass-through)",
                       status, r->connection->number);
        r->headers_out.status = status;
        return NGX_OK;
    }

    if (ngx_http_status_validate(status) != NGX_OK) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http status set rejected invalid status %ui c:%uA",
                       status, r->connection->number);
        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http status set %ui c:%uA", status, r->connection->number);

    r->headers_out.status = status;

    return NGX_OK;
}

#endif


/*
 * One-time, idempotent registry initialization invoked from a pre-fork HTTP
 * postconfiguration hook (ngx_http_header_filter_init), which runs in the
 * master process while the configuration is parsed and therefore before any
 * worker is forked.  The core registry is static const and already valid, so
 * no allocation is required; marking the registry initialized freezes
 * ngx_http_status_register() so that every worker forked afterwards inherits
 * the same read-only registry, including codes added before this point.
 */
ngx_int_t
ngx_http_status_init(void)
{
    if (ngx_http_status_initialized) {
        return NGX_OK;
    }

    ngx_http_status_initialized = 1;

    return NGX_OK;
}
