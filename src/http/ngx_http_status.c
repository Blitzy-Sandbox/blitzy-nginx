
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/*
 * ngx_http_status.c - single source of truth for HTTP status-code metadata.
 *
 * Holds the read-only registry of status-code descriptors (numeric value,
 * reason phrase, RFC 9110 class + cacheability flags, and RFC section) and
 * implements the API that mediates status lookup and assignment.  The reason
 * phrases reproduce the legacy ngx_http_status_lines[] table byte-for-byte so
 * that wire status lines remain identical; codes that were emitted numeric-only
 * (and all 1xx and nginx extension codes) carry an empty reason.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Maximum number of additional status definitions that may be registered at
 * runtime through ngx_http_status_register().  Registration is expected to
 * happen before the worker fork; the registry is treated as read-only after
 * initialization completes.
 */
#define NGX_HTTP_STATUS_MAX_REGISTERED  32


/*
 * The core status-code registry.
 *
 * Entries are kept in ascending code order, grouped by class region, in the
 * exact order assumed by ngx_http_status_index() so that the common lookup is
 * O(1) array indexing.  Because the array is "static const" it lives in the
 * binary's read-only data segment, is shared across all workers without any
 * copy-on-write fault, and contributes effectively zero incremental per-worker
 * RSS (well under the 1 KB budget).
 *
 * The "reason" field is the complete status-line text as it appears on the
 * wire, including the numeric prefix (e.g. "404 Not Found").  Codes that the
 * legacy table emitted numeric-only - together with all 1xx informational and
 * the nginx extension codes - carry ngx_null_string so the header filter keeps
 * producing a numeric-only status line for them.
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
    { 300, ngx_null_string,                          NGX_HTTP_STATUS_CACHEABLE, "15.4.1" },
    { 301, ngx_string("301 Moved Permanently"),      NGX_HTTP_STATUS_CACHEABLE, "15.4.2" },
    { 302, ngx_string("302 Moved Temporarily"),      0, "15.4.3" },
    { 303, ngx_string("303 See Other"),              0, "15.4.4" },
    { 304, ngx_string("304 Not Modified"),           0, "15.4.5" },
    { 305, ngx_null_string,                          0, "15.4.6" },
    { 306, ngx_null_string,                          0, NULL },
    { 307, ngx_string("307 Temporary Redirect"),     0, "15.4.8" },
    { 308, ngx_string("308 Permanent Redirect"),     NGX_HTTP_STATUS_CACHEABLE, "15.4.9" },

    /* 4xx - contiguous 400..429 (gaps carry empty reason for O(1) indexing) */
    { 400, ngx_string("400 Bad Request"),                       NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.1" },
    { 401, ngx_string("401 Unauthorized"),                      NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.2" },
    { 402, ngx_string("402 Payment Required"),                  NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.3" },
    { 403, ngx_string("403 Forbidden"),                         NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.4" },
    { 404, ngx_string("404 Not Found"),                         NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.5" },
    { 405, ngx_string("405 Not Allowed"),                       NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.6" },
    { 406, ngx_string("406 Not Acceptable"),                    NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.7" },
    { 407, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.8" },
    { 408, ngx_string("408 Request Time-out"),                  NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.9" },
    { 409, ngx_string("409 Conflict"),                          NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.10" },
    { 410, ngx_string("410 Gone"),                              NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.11" },
    { 411, ngx_string("411 Length Required"),                   NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.12" },
    { 412, ngx_string("412 Precondition Failed"),               NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.13" },
    { 413, ngx_string("413 Request Entity Too Large"),          NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.14" },
    { 414, ngx_string("414 Request-URI Too Large"),             NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.5.15" },
    { 415, ngx_string("415 Unsupported Media Type"),            NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.16" },
    { 416, ngx_string("416 Requested Range Not Satisfiable"),   NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.17" },
    { 417, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.18" },
    { 418, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 419, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 420, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 421, ngx_string("421 Misdirected Request"),               NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.20" },
    { 422, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.21" },
    { 423, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 424, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 425, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 426, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, "15.5.22" },
    { 427, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 428, ngx_null_string,                                     NGX_HTTP_STATUS_CLIENT_ERROR, NULL },
    { 429, ngx_string("429 Too Many Requests"),                 NGX_HTTP_STATUS_CLIENT_ERROR, NULL },

    /* 5xx */
    { 500, ngx_string("500 Internal Server Error"),            NGX_HTTP_STATUS_SERVER_ERROR, "15.6.1" },
    { 501, ngx_string("501 Not Implemented"),                  NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE, "15.6.2" },
    { 502, ngx_string("502 Bad Gateway"),                      NGX_HTTP_STATUS_SERVER_ERROR, "15.6.3" },
    { 503, ngx_string("503 Service Temporarily Unavailable"),  NGX_HTTP_STATUS_SERVER_ERROR, "15.6.4" },
    { 504, ngx_string("504 Gateway Time-out"),                 NGX_HTTP_STATUS_SERVER_ERROR, "15.6.5" },
    { 505, ngx_string("505 HTTP Version Not Supported"),       NGX_HTTP_STATUS_SERVER_ERROR, "15.6.6" },
    { 506, ngx_null_string,                                     NGX_HTTP_STATUS_SERVER_ERROR, NULL },
    { 507, ngx_string("507 Insufficient Storage"),             NGX_HTTP_STATUS_SERVER_ERROR, NULL },

    /* nginx extension codes - reason empty; preserved behaviors live in their callers */
    { 444, ngx_null_string, 0, NULL },                           /* NGX_HTTP_CLOSE */
    { 494, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* REQUEST_HEADER_TOO_LARGE */
    { 495, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* HTTPS_CERT_ERROR */
    { 496, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* HTTPS_NO_CERT */
    { 497, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* TO_HTTPS */
    { 498, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }, /* canceled */
    { 499, ngx_null_string, NGX_HTTP_STATUS_CLIENT_ERROR, NULL }  /* CLIENT_CLOSED_REQUEST */
};


/*
 * Extensibility table for status definitions registered at runtime through
 * ngx_http_status_register().  It is file-static (mutable only during the
 * pre-fork registration window) and is searched only when a code is not found
 * in the core registry above, keeping the common path O(1).
 */
static ngx_http_status_def_t  ngx_http_status_registered[NGX_HTTP_STATUS_MAX_REGISTERED];
static ngx_uint_t             ngx_http_status_nregistered = 0;


/*
 * Finalization flag for the registry lifecycle.  It is set by
 * ngx_http_status_init() once, before the worker fork, after which the registry
 * is treated as read-only: ngx_http_status_register() rejects any further
 * mutation.  Keeping this state at file scope (rather than local to
 * ngx_http_status_init()) is what lets registration actually enforce - not just
 * document - the pre-fork, read-only-after-init contract, eliminating the
 * worker-time mutation/race risk.
 */
static ngx_uint_t             ngx_http_status_initialized = 0;


/*
 * Map a status code to its index in ngx_http_status_defs[].  The branches are
 * arranged so that each supported region resolves with a single comparison and
 * a subtraction (O(1)); codes outside the registered regions return -1.  The
 * arithmetic MUST stay in lock-step with the array layout above.
 */

static ngx_int_t
ngx_http_status_index(ngx_uint_t status)
{
    if (status >= 100 && status <= 103) return (ngx_int_t) (status - 100);        /* 0..3   */
    if (status >= 200 && status <= 206) return (ngx_int_t) (status - 200 + 4);    /* 4..10  */
    if (status >= 300 && status <= 308) return (ngx_int_t) (status - 300 + 11);   /* 11..19 */
    if (status >= 400 && status <= 429) return (ngx_int_t) (status - 400 + 20);   /* 20..49 */
    if (status >= 500 && status <= 507) return (ngx_int_t) (status - 500 + 50);   /* 50..57 */
    if (status == 444)                  return 58;
    if (status >= 494 && status <= 499) return (ngx_int_t) (status - 494 + 59);   /* 59..64 */

    return -1;
}


/*
 * Resolve a status code to its descriptor.  The core registry is consulted
 * first via the O(1) index helper; only on a miss is the small, bounded
 * runtime-registration table scanned linearly.  Returns NULL when the code is
 * unknown.  The returned pointer references storage that lives for the lifetime
 * of the process, so callers may safely retain the reason ngx_str_t by value.
 */

static const ngx_http_status_def_t *
ngx_http_status_lookup(ngx_uint_t status)
{
    ngx_int_t   idx;
    ngx_uint_t  i;

    idx = ngx_http_status_index(status);

    if (idx >= 0) {
        return &ngx_http_status_defs[idx];
    }

    for (i = 0; i < ngx_http_status_nregistered; i++) {
        if (ngx_http_status_registered[i].code == status) {
            return &ngx_http_status_registered[i];
        }
    }

    return NULL;
}


/*
 * Return the full status-line text for a code, or an empty ngx_str_t for
 * numeric-only codes and for any code not present in the registry.  This is the
 * registry-backed replacement for the header filter's offset arithmetic over
 * ngx_http_status_lines[]; an empty result tells the header filter to emit a
 * numeric-only status line, preserving byte-for-byte wire output.
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
 * Structural RFC 9110 conformance check: a status code is valid when it falls
 * within the defined three-digit class range (100..599).  This accepts every
 * standard code as well as the nginx extension codes (444, 494-499); codes that
 * are in-range but unknown to the registry are still structurally valid and are
 * handled permissively by ngx_http_status_set().
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
 * Report whether a status code carries the cacheable-by-default metadata flag.
 * This exposes the registry metadata only; it introduces no caching behavior of
 * its own.  Returns 1 for a cacheable code, 0 otherwise (including unknown
 * codes).
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
 * Register an additional status definition.  Intended to be called before the
 * worker fork (the registry is read-only afterwards).  The descriptor is copied
 * by value into the bounded registration table.  Returns NGX_ERROR for a NULL
 * descriptor or when the table is full, NGX_OK on success.
 */

ngx_int_t
ngx_http_status_register(const ngx_http_status_def_t *def)
{
    if (ngx_http_status_initialized) {
        /*
         * The registry was finalized by ngx_http_status_init() before the
         * worker fork and is now read-only; reject the late registration so a
         * worker can never mutate the shared registry (no post-fork races).
         */
        return NGX_ERROR;
    }

    if (def == NULL) {
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
 * Assignment chokepoint for the strict-validation build.  Proxied responses
 * bypass validation so an upstream-supplied status is preserved verbatim
 * (pass-through).  Otherwise the code is validated before it is stored; on
 * failure the function returns NGX_ERROR WITHOUT logging, leaving the caller to
 * emit the NGX_LOG_ERR and fall back to NGX_HTTP_INTERNAL_SERVER_ERROR.
 */

ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
    if (r->upstream != NULL) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http status set (upstream pass-through): %ui", status);
        r->headers_out.status = status;
        return NGX_OK;
    }

    if (ngx_http_status_validate(status) != NGX_OK) {
        return NGX_ERROR;
    }

    /*
     * Successful non-upstream assignment: emit the same status-set debug trace
     * as the default-build inline setter and the upstream pass-through branch,
     * giving a consistent, connection-correlated log shape across every path.
     * The invalid path above intentionally stays silent so the caller owns the
     * single NGX_LOG_ERR "invalid HTTP status" record.
     */
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http status set: %ui", status);

    r->headers_out.status = status;

    return NGX_OK;
}

#endif


/*
 * One-time, idempotent initialization of the status registry.  Called once
 * before the worker fork so that every worker inherits the read-only registry
 * (including any codes added via ngx_http_status_register()).  The core
 * registry is static const and therefore already valid, so there is nothing to
 * allocate here.
 */

ngx_int_t
ngx_http_status_init(void)
{
    if (ngx_http_status_initialized) {
        return NGX_OK;
    }

    /*
     * Finalize the registry: from this point on it is read-only and
     * ngx_http_status_register() rejects further mutation.  Because this runs
     * once before the worker fork, every worker inherits the same finalized,
     * read-only registry.
     */
    ngx_http_status_initialized = 1;

    return NGX_OK;
}

