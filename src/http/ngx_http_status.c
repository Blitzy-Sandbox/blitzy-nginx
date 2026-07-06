
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Centralized HTTP status registry.  The table is statically initialized and
 * read-only after load, so all worker processes share it without locking.
 * Each entry carries the numeric code, the wire reason phrase, the class and
 * behavior flags from ngx_http_status.h, and an RFC 9110 section reference.
 * The reason phrases match the status lines emitted by the header filter.
 */

static ngx_http_status_def_t  ngx_http_status_defs[] = {

    /* 1xx informational (RFC 9110 15.2) */
    { 100, ngx_string("Continue"),
      NGX_HTTP_STATUS_INFORMATIONAL, "RFC 9110 15.2.1" },
    { 101, ngx_string("Switching Protocols"),
      NGX_HTTP_STATUS_INFORMATIONAL, "RFC 9110 15.2.2" },

    /* 2xx successful (RFC 9110 15.3) */
    { 200, ngx_string("OK"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC 9110 15.3.1" },
    { 201, ngx_string("Created"),
      0, "RFC 9110 15.3.2" },
    { 202, ngx_string("Accepted"),
      0, "RFC 9110 15.3.3" },
    { 204, ngx_string("No Content"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC 9110 15.3.5" },
    { 206, ngx_string("Partial Content"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC 9110 15.3.7" },

    /* 3xx redirection (RFC 9110 15.4) */
    { 301, ngx_string("Moved Permanently"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC 9110 15.4.2" },
    { 302, ngx_string("Moved Temporarily"),
      0, "RFC 9110 15.4.3" },
    { 303, ngx_string("See Other"),
      0, "RFC 9110 15.4.4" },
    { 304, ngx_string("Not Modified"),
      0, "RFC 9110 15.4.5" },
    { 307, ngx_string("Temporary Redirect"),
      0, "RFC 9110 15.4.8" },
    { 308, ngx_string("Permanent Redirect"),
      NGX_HTTP_STATUS_CACHEABLE, "RFC 9110 15.4.9" },

    /* 4xx client error (RFC 9110 15.5) */
    { 400, ngx_string("Bad Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.1" },
    { 401, ngx_string("Unauthorized"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.2" },
    { 402, ngx_string("Payment Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.3" },
    { 403, ngx_string("Forbidden"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.4" },
    { 404, ngx_string("Not Found"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 15.5.5" },
    { 405, ngx_string("Not Allowed"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 15.5.6" },
    { 406, ngx_string("Not Acceptable"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.7" },
    { 408, ngx_string("Request Time-out"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.9" },
    { 409, ngx_string("Conflict"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.10" },
    { 410, ngx_string("Gone"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 15.5.11" },
    { 411, ngx_string("Length Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.12" },
    { 412, ngx_string("Precondition Failed"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.13" },
    { 413, ngx_string("Request Entity Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.14" },
    { 414, ngx_string("Request-URI Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 15.5.15" },
    { 415, ngx_string("Unsupported Media Type"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.16" },
    { 416, ngx_string("Requested Range Not Satisfiable"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 9110 15.5.17" },
    { 421, ngx_string("Misdirected Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 15.5.20" },
    { 429, ngx_string("Too Many Requests"),
      NGX_HTTP_STATUS_CLIENT_ERROR, "RFC 6585 4" },

    /* 5xx server error (RFC 9110 15.6) */
    { 500, ngx_string("Internal Server Error"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC 9110 15.6.1" },
    { 501, ngx_string("Not Implemented"),
      NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 15.6.2" },
    { 502, ngx_string("Bad Gateway"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC 9110 15.6.3" },
    { 503, ngx_string("Service Temporarily Unavailable"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC 9110 15.6.4" },
    { 504, ngx_string("Gateway Time-out"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC 9110 15.6.5" },
    { 505, ngx_string("HTTP Version Not Supported"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC 9110 15.6.6" },
    { 507, ngx_string("Insufficient Storage"),
      NGX_HTTP_STATUS_SERVER_ERROR, "RFC 4918 11.5" }
};


static ngx_http_status_def_t *
ngx_http_status_lookup(ngx_uint_t status)
{
    ngx_uint_t  i, n;

    n = sizeof(ngx_http_status_defs) / sizeof(ngx_http_status_def_t);

    for (i = 0; i < n; i++) {
        if (ngx_http_status_defs[i].code == status) {
            return &ngx_http_status_defs[i];
        }
    }

    return NULL;
}


ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
#if (NGX_HTTP_STATUS_VALIDATION)

    /*
     * Upstream-origin status codes pass through unvalidated so that
     * non-standard backend codes are never rejected or transformed.
     */

    if (r->upstream == NULL && ngx_http_status_validate(status) != NGX_OK) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "rejected out-of-range HTTP status %ui", status);
        return NGX_ERROR;
    }

#endif

    r->headers_out.status = status;

    return NGX_OK;
}


ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
    /* RFC 9110 section 15 defines valid status codes in the range 100..599 */

    if (status < 100 || status > 599) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_str_t
ngx_http_status_reason(ngx_uint_t status)
{
    ngx_http_status_def_t  *def;

    static ngx_str_t  none = ngx_null_string;

    def = ngx_http_status_lookup(status);

    if (def != NULL) {
        return def->reason;
    }

    return none;
}


ngx_int_t
ngx_http_status_register(void)
{
    /*
     * The registry is a statically initialized, read-only table and is ready
     * as soon as the module is loaded; nothing is allocated at run time.
     */

    return NGX_OK;
}


ngx_uint_t
ngx_http_status_is_cacheable(ngx_uint_t status)
{
    ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def != NULL && (def->flags & NGX_HTTP_STATUS_CACHEABLE)) {
        return 1;
    }

    return 0;
}
