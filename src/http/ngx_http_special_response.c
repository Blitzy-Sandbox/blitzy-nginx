
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>


static ngx_int_t ngx_http_send_error_page(ngx_http_request_t *r,
    ngx_http_err_page_t *err_page);
static ngx_int_t ngx_http_send_special_response(ngx_http_request_t *r,
    ngx_http_core_loc_conf_t *clcf, ngx_uint_t status);
static ngx_str_t ngx_http_error_page_body(ngx_uint_t status);
static ngx_int_t ngx_http_send_refresh(ngx_http_request_t *r);


static u_char ngx_http_error_full_tail[] =
"<hr><center>" NGINX_VER "</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;


static u_char ngx_http_error_build_tail[] =
"<hr><center>" NGINX_VER_BUILD "</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;


static u_char ngx_http_error_tail[] =
"<hr><center>nginx</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;


static u_char ngx_http_msie_padding[] =
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
;


static u_char ngx_http_msie_refresh_head[] =
"<html><head><meta http-equiv=\"Refresh\" content=\"0; URL=";


static u_char ngx_http_msie_refresh_tail[] =
"\"></head><body></body></html>" CRLF;


static char ngx_http_error_301_page[] =
"<html>" CRLF
"<head><title>301 Moved Permanently</title></head>" CRLF
"<body>" CRLF
"<center><h1>301 Moved Permanently</h1></center>" CRLF
;


static char ngx_http_error_302_page[] =
"<html>" CRLF
"<head><title>302 Found</title></head>" CRLF
"<body>" CRLF
"<center><h1>302 Found</h1></center>" CRLF
;


static char ngx_http_error_303_page[] =
"<html>" CRLF
"<head><title>303 See Other</title></head>" CRLF
"<body>" CRLF
"<center><h1>303 See Other</h1></center>" CRLF
;


static char ngx_http_error_307_page[] =
"<html>" CRLF
"<head><title>307 Temporary Redirect</title></head>" CRLF
"<body>" CRLF
"<center><h1>307 Temporary Redirect</h1></center>" CRLF
;


static char ngx_http_error_308_page[] =
"<html>" CRLF
"<head><title>308 Permanent Redirect</title></head>" CRLF
"<body>" CRLF
"<center><h1>308 Permanent Redirect</h1></center>" CRLF
;


static char ngx_http_error_400_page[] =
"<html>" CRLF
"<head><title>400 Bad Request</title></head>" CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
;


static char ngx_http_error_401_page[] =
"<html>" CRLF
"<head><title>401 Authorization Required</title></head>" CRLF
"<body>" CRLF
"<center><h1>401 Authorization Required</h1></center>" CRLF
;


static char ngx_http_error_402_page[] =
"<html>" CRLF
"<head><title>402 Payment Required</title></head>" CRLF
"<body>" CRLF
"<center><h1>402 Payment Required</h1></center>" CRLF
;


static char ngx_http_error_403_page[] =
"<html>" CRLF
"<head><title>403 Forbidden</title></head>" CRLF
"<body>" CRLF
"<center><h1>403 Forbidden</h1></center>" CRLF
;


static char ngx_http_error_404_page[] =
"<html>" CRLF
"<head><title>404 Not Found</title></head>" CRLF
"<body>" CRLF
"<center><h1>404 Not Found</h1></center>" CRLF
;


static char ngx_http_error_405_page[] =
"<html>" CRLF
"<head><title>405 Not Allowed</title></head>" CRLF
"<body>" CRLF
"<center><h1>405 Not Allowed</h1></center>" CRLF
;


static char ngx_http_error_406_page[] =
"<html>" CRLF
"<head><title>406 Not Acceptable</title></head>" CRLF
"<body>" CRLF
"<center><h1>406 Not Acceptable</h1></center>" CRLF
;


static char ngx_http_error_408_page[] =
"<html>" CRLF
"<head><title>408 Request Time-out</title></head>" CRLF
"<body>" CRLF
"<center><h1>408 Request Time-out</h1></center>" CRLF
;


static char ngx_http_error_409_page[] =
"<html>" CRLF
"<head><title>409 Conflict</title></head>" CRLF
"<body>" CRLF
"<center><h1>409 Conflict</h1></center>" CRLF
;


static char ngx_http_error_410_page[] =
"<html>" CRLF
"<head><title>410 Gone</title></head>" CRLF
"<body>" CRLF
"<center><h1>410 Gone</h1></center>" CRLF
;


static char ngx_http_error_411_page[] =
"<html>" CRLF
"<head><title>411 Length Required</title></head>" CRLF
"<body>" CRLF
"<center><h1>411 Length Required</h1></center>" CRLF
;


static char ngx_http_error_412_page[] =
"<html>" CRLF
"<head><title>412 Precondition Failed</title></head>" CRLF
"<body>" CRLF
"<center><h1>412 Precondition Failed</h1></center>" CRLF
;


static char ngx_http_error_413_page[] =
"<html>" CRLF
"<head><title>413 Request Entity Too Large</title></head>" CRLF
"<body>" CRLF
"<center><h1>413 Request Entity Too Large</h1></center>" CRLF
;


static char ngx_http_error_414_page[] =
"<html>" CRLF
"<head><title>414 Request-URI Too Large</title></head>" CRLF
"<body>" CRLF
"<center><h1>414 Request-URI Too Large</h1></center>" CRLF
;


static char ngx_http_error_415_page[] =
"<html>" CRLF
"<head><title>415 Unsupported Media Type</title></head>" CRLF
"<body>" CRLF
"<center><h1>415 Unsupported Media Type</h1></center>" CRLF
;


static char ngx_http_error_416_page[] =
"<html>" CRLF
"<head><title>416 Requested Range Not Satisfiable</title></head>" CRLF
"<body>" CRLF
"<center><h1>416 Requested Range Not Satisfiable</h1></center>" CRLF
;


static char ngx_http_error_421_page[] =
"<html>" CRLF
"<head><title>421 Misdirected Request</title></head>" CRLF
"<body>" CRLF
"<center><h1>421 Misdirected Request</h1></center>" CRLF
;


static char ngx_http_error_429_page[] =
"<html>" CRLF
"<head><title>429 Too Many Requests</title></head>" CRLF
"<body>" CRLF
"<center><h1>429 Too Many Requests</h1></center>" CRLF
;


static char ngx_http_error_494_page[] =
"<html>" CRLF
"<head><title>400 Request Header Or Cookie Too Large</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>Request Header Or Cookie Too Large</center>" CRLF
;


static char ngx_http_error_495_page[] =
"<html>" CRLF
"<head><title>400 The SSL certificate error</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>The SSL certificate error</center>" CRLF
;


static char ngx_http_error_496_page[] =
"<html>" CRLF
"<head><title>400 No required SSL certificate was sent</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>No required SSL certificate was sent</center>" CRLF
;


static char ngx_http_error_497_page[] =
"<html>" CRLF
"<head><title>400 The plain HTTP request was sent to HTTPS port</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>The plain HTTP request was sent to HTTPS port</center>" CRLF
;


static char ngx_http_error_500_page[] =
"<html>" CRLF
"<head><title>500 Internal Server Error</title></head>" CRLF
"<body>" CRLF
"<center><h1>500 Internal Server Error</h1></center>" CRLF
;


static char ngx_http_error_501_page[] =
"<html>" CRLF
"<head><title>501 Not Implemented</title></head>" CRLF
"<body>" CRLF
"<center><h1>501 Not Implemented</h1></center>" CRLF
;


static char ngx_http_error_502_page[] =
"<html>" CRLF
"<head><title>502 Bad Gateway</title></head>" CRLF
"<body>" CRLF
"<center><h1>502 Bad Gateway</h1></center>" CRLF
;


static char ngx_http_error_503_page[] =
"<html>" CRLF
"<head><title>503 Service Temporarily Unavailable</title></head>" CRLF
"<body>" CRLF
"<center><h1>503 Service Temporarily Unavailable</h1></center>" CRLF
;


static char ngx_http_error_504_page[] =
"<html>" CRLF
"<head><title>504 Gateway Time-out</title></head>" CRLF
"<body>" CRLF
"<center><h1>504 Gateway Time-out</h1></center>" CRLF
;


static char ngx_http_error_505_page[] =
"<html>" CRLF
"<head><title>505 HTTP Version Not Supported</title></head>" CRLF
"<body>" CRLF
"<center><h1>505 HTTP Version Not Supported</h1></center>" CRLF
;


static char ngx_http_error_507_page[] =
"<html>" CRLF
"<head><title>507 Insufficient Storage</title></head>" CRLF
"<body>" CRLF
"<center><h1>507 Insufficient Storage</h1></center>" CRLF
;


/*
 * Error-page body table, aligned to the status registry.  Each entry is keyed
 * by its registry status code rather than by the former per-class offset
 * arithmetic, so the table is self-describing and no longer maintains a second,
 * independently-synchronized encoding of the code-space structure.  Codes that
 * have no built-in body are simply absent: ngx_http_error_page_body() returns an
 * empty string for them, rendered as a zero-length body exactly as the previous
 * ngx_null_string placeholders were.  The bodies are the unchanged static byte
 * arrays above, so the wire output is byte-identical to previous releases.
 */

typedef struct {
    ngx_uint_t  status;
    ngx_str_t   page;
} ngx_http_error_page_entry_t;


static ngx_http_error_page_entry_t  ngx_http_error_pages[] = {

    /* 3xx redirects (300/304/305/306 have no built-in body) */
    { 301, ngx_string(ngx_http_error_301_page) },
    { 302, ngx_string(ngx_http_error_302_page) },
    { 303, ngx_string(ngx_http_error_303_page) },
    { 307, ngx_string(ngx_http_error_307_page) },
    { 308, ngx_string(ngx_http_error_308_page) },

    /* 4xx client errors (407, 417-420, 422-428 have no built-in body) */
    { 400, ngx_string(ngx_http_error_400_page) },
    { 401, ngx_string(ngx_http_error_401_page) },
    { 402, ngx_string(ngx_http_error_402_page) },
    { 403, ngx_string(ngx_http_error_403_page) },
    { 404, ngx_string(ngx_http_error_404_page) },
    { 405, ngx_string(ngx_http_error_405_page) },
    { 406, ngx_string(ngx_http_error_406_page) },
    { 408, ngx_string(ngx_http_error_408_page) },
    { 409, ngx_string(ngx_http_error_409_page) },
    { 410, ngx_string(ngx_http_error_410_page) },
    { 411, ngx_string(ngx_http_error_411_page) },
    { 412, ngx_string(ngx_http_error_412_page) },
    { 413, ngx_string(ngx_http_error_413_page) },
    { 414, ngx_string(ngx_http_error_414_page) },
    { 415, ngx_string(ngx_http_error_415_page) },
    { 416, ngx_string(ngx_http_error_416_page) },
    { 421, ngx_string(ngx_http_error_421_page) },
    { 429, ngx_string(ngx_http_error_429_page) },

    /* nginx extension codes (499 has no built-in body) */
    { 494, ngx_string(ngx_http_error_494_page) }, /* request header too large */
    { 495, ngx_string(ngx_http_error_495_page) }, /* https certificate error */
    { 496, ngx_string(ngx_http_error_496_page) }, /* https no certificate */
    { 497, ngx_string(ngx_http_error_497_page) }, /* http to https */
    { 498, ngx_string(ngx_http_error_404_page) }, /* canceled, reuses 404 body */

    /* 5xx server errors (506 has no built-in body) */
    { 500, ngx_string(ngx_http_error_500_page) },
    { 501, ngx_string(ngx_http_error_501_page) },
    { 502, ngx_string(ngx_http_error_502_page) },
    { 503, ngx_string(ngx_http_error_503_page) },
    { 504, ngx_string(ngx_http_error_504_page) },
    { 505, ngx_string(ngx_http_error_505_page) },
    { 507, ngx_string(ngx_http_error_507_page) }

};


/*
 * Registry-fed error-page lookup.  The status registry is the single source of
 * truth for which codes exist: a built-in body is rendered only for a
 * registry-valid code (ngx_http_status_validate), and the body is then selected
 * by status code from the table above.  Returns an empty ngx_str_t for any code
 * without a built-in body, which the caller renders as a zero-length body.
 */
static ngx_str_t
ngx_http_error_page_body(ngx_uint_t status)
{
    ngx_uint_t  i, n;
    ngx_str_t   body;

    ngx_str_null(&body);

    if (ngx_http_status_validate(status) != NGX_OK) {
        return body;
    }

    n = sizeof(ngx_http_error_pages) / sizeof(ngx_http_error_pages[0]);

    for (i = 0; i < n; i++) {
        if (ngx_http_error_pages[i].status == status) {
            return ngx_http_error_pages[i].page;
        }
    }

    return body;
}


ngx_int_t
ngx_http_special_response_handler(ngx_http_request_t *r, ngx_int_t error)
{
    ngx_uint_t                 i;
    ngx_http_err_page_t       *err_page;
    ngx_http_core_loc_conf_t  *clcf;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http special response: %i, \"%V?%V\"",
                   error, &r->uri, &r->args);

    r->err_status = error;

    if (r->keepalive) {
        switch (error) {
            case NGX_HTTP_BAD_REQUEST:
            case NGX_HTTP_REQUEST_ENTITY_TOO_LARGE:
            case NGX_HTTP_REQUEST_URI_TOO_LARGE:
            case NGX_HTTP_TO_HTTPS:
            case NGX_HTTPS_CERT_ERROR:
            case NGX_HTTPS_NO_CERT:
            case NGX_HTTP_INTERNAL_SERVER_ERROR:
            case NGX_HTTP_NOT_IMPLEMENTED:
                r->keepalive = 0;
        }
    }

    if (r->lingering_close) {
        switch (error) {
            case NGX_HTTP_BAD_REQUEST:
            case NGX_HTTP_TO_HTTPS:
            case NGX_HTTPS_CERT_ERROR:
            case NGX_HTTPS_NO_CERT:
                r->lingering_close = 0;
        }
    }

    r->headers_out.content_type.len = 0;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (!r->error_page && clcf->error_pages && r->uri_changes != 0) {

        if (clcf->recursive_error_pages == 0) {
            r->error_page = 1;
        }

        err_page = clcf->error_pages->elts;

        for (i = 0; i < clcf->error_pages->nelts; i++) {
            if (err_page[i].status == error) {
                return ngx_http_send_error_page(r, &err_page[i]);
            }
        }
    }

    r->expect_tested = 1;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        r->keepalive = 0;
    }

    if (clcf->msie_refresh
        && r->headers_in.msie
        && (error == NGX_HTTP_MOVED_PERMANENTLY
            || error == NGX_HTTP_MOVED_TEMPORARILY))
    {
        return ngx_http_send_refresh(r);
    }

    /*
     * Preserve the nginx security wire-rewrite: the internal HTTPS / oversized
     * request-header codes are reported to the client as 400 (Bad Request).
     * Only the status line changes here -- the error-page body is still selected
     * by the original code, because ngx_http_send_special_response() is given
     * the original "error" (not the rewritten r->err_status).  So 494/495/496/497
     * continue to render their own bodies exactly as before.
     */
    switch (error) {
    case NGX_HTTP_TO_HTTPS:
    case NGX_HTTPS_CERT_ERROR:
    case NGX_HTTPS_NO_CERT:
    case NGX_HTTP_REQUEST_HEADER_TOO_LARGE:
        r->err_status = NGX_HTTP_BAD_REQUEST;
        break;

    default:
        break;
    }

    return ngx_http_send_special_response(r, clcf, error);
}


ngx_int_t
ngx_http_filter_finalize_request(ngx_http_request_t *r, ngx_module_t *m,
    ngx_int_t error)
{
    void       *ctx;
    ngx_int_t   rc;

    ngx_http_clean_header(r);

    ctx = NULL;

    if (m) {
        ctx = r->ctx[m->ctx_index];
    }

    /* clear the modules contexts */
    ngx_memzero(r->ctx, sizeof(void *) * ngx_http_max_module);

    if (m) {
        r->ctx[m->ctx_index] = ctx;
    }

    r->filter_finalize = 1;

    rc = ngx_http_special_response_handler(r, error);

    /* NGX_ERROR resets any pending data */

    switch (rc) {

    case NGX_OK:
    case NGX_DONE:
        return NGX_ERROR;

    default:
        return rc;
    }
}


void
ngx_http_clean_header(ngx_http_request_t *r)
{
    ngx_memzero(&r->headers_out.status,
                sizeof(ngx_http_headers_out_t)
                    - offsetof(ngx_http_headers_out_t, status));

    r->headers_out.headers.part.nelts = 0;
    r->headers_out.headers.part.next = NULL;
    r->headers_out.headers.last = &r->headers_out.headers.part;

    r->headers_out.trailers.part.nelts = 0;
    r->headers_out.trailers.part.next = NULL;
    r->headers_out.trailers.last = &r->headers_out.trailers.part;

    r->headers_out.content_length_n = -1;
    r->headers_out.last_modified_time = -1;
}


static ngx_int_t
ngx_http_send_error_page(ngx_http_request_t *r, ngx_http_err_page_t *err_page)
{
    ngx_int_t                  overwrite;
    ngx_str_t                  uri, args;
    ngx_table_elt_t           *location;
    ngx_http_core_loc_conf_t  *clcf;

    overwrite = err_page->overwrite;

    if (overwrite && overwrite != NGX_HTTP_OK) {
        r->expect_tested = 1;
    }

    if (overwrite >= 0) {
        r->err_status = overwrite;
    }

    if (ngx_http_complex_value(r, &err_page->value, &uri) != NGX_OK) {
        return NGX_ERROR;
    }

    if (uri.len && uri.data[0] == '/') {

        if (err_page->value.lengths) {
            ngx_http_split_args(r, &uri, &args);

        } else {
            args = err_page->args;
        }

        if (r->method != NGX_HTTP_HEAD) {
            r->method = NGX_HTTP_GET;
            r->method_name = ngx_http_core_get_method;
        }

        return ngx_http_internal_redirect(r, &uri, &args);
    }

    if (uri.len && uri.data[0] == '@') {
        return ngx_http_named_location(r, &uri);
    }

    r->expect_tested = 1;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        r->keepalive = 0;
    }

    location = ngx_list_push(&r->headers_out.headers);

    if (location == NULL) {
        return NGX_ERROR;
    }

    if (overwrite != NGX_HTTP_MOVED_PERMANENTLY
        && overwrite != NGX_HTTP_MOVED_TEMPORARILY
        && overwrite != NGX_HTTP_SEE_OTHER
        && overwrite != NGX_HTTP_TEMPORARY_REDIRECT
        && overwrite != NGX_HTTP_PERMANENT_REDIRECT)
    {
        r->err_status = NGX_HTTP_MOVED_TEMPORARILY;
    }

    location->hash = 1;
    location->next = NULL;
    ngx_str_set(&location->key, "Location");
    location->value = uri;

    ngx_http_clear_location(r);

    r->headers_out.location = location;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->msie_refresh && r->headers_in.msie) {
        return ngx_http_send_refresh(r);
    }

    /*
     * The error_page redirect path always ends on a 3xx status (r->err_status
     * has been normalized to a redirect code above); pass that code straight to
     * the registry-fed renderer, which selects the matching redirect body.
     */
    return ngx_http_send_special_response(r, clcf, r->err_status);
}


static ngx_int_t
ngx_http_send_special_response(ngx_http_request_t *r,
    ngx_http_core_loc_conf_t *clcf, ngx_uint_t status)
{
    u_char       *tail;
    size_t        len;
    ngx_int_t     rc;
    ngx_str_t     body;
    ngx_buf_t    *b;
    ngx_uint_t    msie_padding;
    ngx_chain_t   out[3];

    /* registry-fed body selection: keyed by status code, no offset arithmetic */
    body = ngx_http_error_page_body(status);

    if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_ON) {
        len = sizeof(ngx_http_error_full_tail) - 1;
        tail = ngx_http_error_full_tail;

    } else if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_BUILD) {
        len = sizeof(ngx_http_error_build_tail) - 1;
        tail = ngx_http_error_build_tail;

    } else {
        len = sizeof(ngx_http_error_tail) - 1;
        tail = ngx_http_error_tail;
    }

    msie_padding = 0;

    if (body.len) {
        r->headers_out.content_length_n = body.len + len;
        /*
         * MSIE/Chrome padding applies to client- and server-error bodies (4xx,
         * 49x, 5xx).  The error class is read from the registry's authoritative
         * flags rather than re-deriving it from a numeric offset, so this single
         * source of truth decides the behavior; the set of codes that satisfy
         * it is identical to the previous "4xx and above" offset test.
         */
        if (clcf->msie_padding
            && (r->headers_in.msie || r->headers_in.chrome)
            && r->http_version >= NGX_HTTP_VERSION_10
            && (ngx_http_status_flags(status)
                & (NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_SERVER_ERROR)))
        {
            r->headers_out.content_length_n +=
                                         sizeof(ngx_http_msie_padding) - 1;
            msie_padding = 1;
        }

        r->headers_out.content_type_len = sizeof("text/html") - 1;
        ngx_str_set(&r->headers_out.content_type, "text/html");
        r->headers_out.content_type_lowcase = NULL;

    } else {
        r->headers_out.content_length_n = 0;
    }

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }

    ngx_http_clear_accept_ranges(r);
    ngx_http_clear_last_modified(r);
    ngx_http_clear_etag(r);

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || r->header_only) {
        return rc;
    }

    if (body.len == 0) {
        return ngx_http_send_special(r, NGX_HTTP_LAST);
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->memory = 1;
    b->pos = body.data;
    b->last = body.data + body.len;

    out[0].buf = b;
    out[0].next = &out[1];

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->memory = 1;

    b->pos = tail;
    b->last = tail + len;

    out[1].buf = b;
    out[1].next = NULL;

    if (msie_padding) {
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }

        b->memory = 1;
        b->pos = ngx_http_msie_padding;
        b->last = ngx_http_msie_padding + sizeof(ngx_http_msie_padding) - 1;

        out[1].next = &out[2];
        out[2].buf = b;
        out[2].next = NULL;
    }

    if (r == r->main) {
        b->last_buf = 1;
    }

    b->last_in_chain = 1;

    return ngx_http_output_filter(r, &out[0]);
}


static ngx_int_t
ngx_http_send_refresh(ngx_http_request_t *r)
{
    u_char       *p, *location;
    size_t        len, size;
    uintptr_t     escape;
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t   out;

    len = r->headers_out.location->value.len;
    location = r->headers_out.location->value.data;

    escape = 2 * ngx_escape_uri(NULL, location, len, NGX_ESCAPE_REFRESH);

    size = sizeof(ngx_http_msie_refresh_head) - 1
           + escape + len
           + sizeof(ngx_http_msie_refresh_tail) - 1;

    r->err_status = NGX_HTTP_OK;

    r->headers_out.content_type_len = sizeof("text/html") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/html");
    r->headers_out.content_type_lowcase = NULL;

    r->headers_out.location->hash = 0;
    r->headers_out.location = NULL;

    r->headers_out.content_length_n = size;

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }

    ngx_http_clear_accept_ranges(r);
    ngx_http_clear_last_modified(r);
    ngx_http_clear_etag(r);

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || r->header_only) {
        return rc;
    }

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(b->pos, ngx_http_msie_refresh_head,
                   sizeof(ngx_http_msie_refresh_head) - 1);

    if (escape == 0) {
        p = ngx_cpymem(p, location, len);

    } else {
        p = (u_char *) ngx_escape_uri(p, location, len, NGX_ESCAPE_REFRESH);
    }

    b->last = ngx_cpymem(p, ngx_http_msie_refresh_tail,
                         sizeof(ngx_http_msie_refresh_tail) - 1);

    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}
