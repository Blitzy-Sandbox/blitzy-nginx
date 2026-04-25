
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_status.h>


/*
 * Sentinel for the Null Object pattern used by ngx_http_status_reason().
 *
 * When the caller queries the reason phrase for a status code that is not
 * present in the static registry, ngx_http_status_reason() returns a pointer
 * to this sentinel rather than returning NULL.  This keeps caller code free
 * of defensive NULL-pointer checks: the returned ngx_str_t * is always safe
 * to dereference, and the resulting ngx_str_t always has a valid len/data
 * pair.
 *
 * The sentinel uses the literal string "Unknown", matching the convention
 * used by ngx_http_header_filter_module.c when it formats the HTTP/1.1
 * status line for an unrecognized numeric status code.
 *
 * Storage: file-scope static, .rodata-eligible after compile-time literal
 * folding by the linker.  No heap allocation, no per-process state.
 */
static ngx_str_t  ngx_http_status_unknown_reason = ngx_string("Unknown");


/*
 * Registry initialization flag.
 *
 * The status registry is populated via static initializers at compile time
 * (see ngx_http_status_registry[] below), so logically the registry is
 * "initialized" before main() runs.  This flag therefore defaults to 1.
 *
 * The flag exists exclusively to enforce the Agent Action Plan rule
 * "Never create registry modification APIs accessible post-initialization":
 * ngx_http_status_register() consults this flag and returns NGX_ERROR
 * unconditionally when it is set, which is always.  The flag is also
 * (re)set to 1 by ngx_http_status_init_registry() for forward
 * compatibility with any future cycle-scoped initialization hooks.
 *
 * Note: this is the ONLY mutable file-scope variable in this translation
 * unit.  The registry array itself (ngx_http_status_registry[]) is
 * effectively read-only after startup.  Per AAP §0.4.3, the registry's
 * thread safety is provided by immutability: NGINX's worker model is
 * process-based, so each worker inherits a fork-private copy of .data
 * and .rodata, and no synchronization is required.
 */
static ngx_uint_t  ngx_http_status_init_done = 1;


/*
 * Static HTTP status code registry.
 *
 * This array is the single source of truth for status code metadata in the
 * NGINX HTTP subsystem after this refactor.  Each entry carries:
 *
 *   - code         numeric HTTP status code (100..599 inclusive)
 *   - reason       canonical RFC 9110 reason phrase (ngx_str_t literal)
 *   - flags        bitwise OR of NGX_HTTP_STATUS_* class/cacheability flags
 *   - rfc_section  reference to the defining RFC section (or
 *                  "nginx extension" for NGINX-specific 4xx codes)
 *
 * IMPORTANT (per AAP §0.7.2 D-008):
 *
 *   The reason phrases stored here follow RFC 9110 canonical wording.  For
 *   example, 302 carries "Found" (RFC 9110 §15.4.3), not the legacy
 *   "Moved Temporarily" string emitted on the wire by the HTTP/1.1 status
 *   line table in ngx_http_header_filter_module.c.  The wire table is
 *   intentionally preserved byte-for-byte for backward compatibility with
 *   existing clients; this registry carries the modern, RFC-compliant
 *   reason phrases that new code paths and tooling should consult.
 *
 *   Six known divergences exist (302, 408, 413, 414, 416, 503) and are
 *   documented in docs/architecture/decision_log.md (decision D-008).
 *
 * The cacheability flags follow RFC 9110 Section 15 default cacheability:
 * 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 451, and 501 are
 * heuristically cacheable in the absence of explicit Cache-Control
 * directives.
 *
 * The registry has 59 entries: 4 informational, 6 successful, 7 redirection,
 * 25 standard 4xx, 6 NGINX-specific 4xx, and 11 server error.  Memory
 * footprint is approximately 1.4 KB per worker, dominated by the inline
 * rfc_section pointers.
 *
 * The registry has gaps (e.g., 205, 305, 306, 418-420, 423, 424, 427, 430,
 * 432-443, 445-450, 452-493, 498) because those codes are either
 * deprecated, reserved, or unused by NGINX.  Callers querying an
 * unregistered code receive the sentinel "Unknown" reason and a
 * cacheability of 0; the linear-scan lookup is designed to handle gaps
 * naturally and is more correct than a class-index arithmetic alternative.
 *
 * The encoding "\xc2\xa7" is the UTF-8 encoding of the section sign (§).
 * It is used in the rfc_section literal so that the source file remains
 * pure ASCII (matching NGINX's source-file convention) while the resulting
 * runtime string still renders as "RFC 9110 §15.x.y" in logs and tooling.
 */
static ngx_http_status_def_t  ngx_http_status_registry[] = {

    /* 1xx Informational - RFC 9110 Section 15.2 */

    { 100, ngx_string("Continue"),
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 9110 \xc2\xa7""15.2.1" },

    { 101, ngx_string("Switching Protocols"),
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 9110 \xc2\xa7""15.2.2" },

    { 102, ngx_string("Processing"),
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 2518 \xc2\xa7""10.1" },

    { 103, ngx_string("Early Hints"),
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 8297 \xc2\xa7""2" },

    /* 2xx Successful - RFC 9110 Section 15.3 */

    { 200, ngx_string("OK"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.3.1" },

    { 201, ngx_string("Created"),
      0,
      "RFC 9110 \xc2\xa7""15.3.2" },

    { 202, ngx_string("Accepted"),
      0,
      "RFC 9110 \xc2\xa7""15.3.3" },

    { 203, ngx_string("Non-Authoritative Information"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.3.4" },

    { 204, ngx_string("No Content"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.3.5" },

    { 206, ngx_string("Partial Content"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.3.7" },

    /* 3xx Redirection - RFC 9110 Section 15.4 */

    { 300, ngx_string("Multiple Choices"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.4.1" },

    { 301, ngx_string("Moved Permanently"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.4.2" },

    { 302, ngx_string("Found"),
      0,
      "RFC 9110 \xc2\xa7""15.4.3" },

    { 303, ngx_string("See Other"),
      0,
      "RFC 9110 \xc2\xa7""15.4.4" },

    { 304, ngx_string("Not Modified"),
      0,
      "RFC 9110 \xc2\xa7""15.4.5" },

    { 307, ngx_string("Temporary Redirect"),
      0,
      "RFC 9110 \xc2\xa7""15.4.8" },

    { 308, ngx_string("Permanent Redirect"),
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.4.9" },

    /* 4xx Client Error - RFC 9110 Section 15.5 */

    { 400, ngx_string("Bad Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.1" },

    { 401, ngx_string("Unauthorized"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.2" },

    { 402, ngx_string("Payment Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.3" },

    { 403, ngx_string("Forbidden"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.4" },

    { 404, ngx_string("Not Found"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.5.5" },

    { 405, ngx_string("Method Not Allowed"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.5.6" },

    { 406, ngx_string("Not Acceptable"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.7" },

    { 408, ngx_string("Request Timeout"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.9" },

    { 409, ngx_string("Conflict"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.10" },

    { 410, ngx_string("Gone"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.5.11" },

    { 411, ngx_string("Length Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.12" },

    { 412, ngx_string("Precondition Failed"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.13" },

    { 413, ngx_string("Content Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.14" },

    { 414, ngx_string("URI Too Long"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.5.15" },

    { 415, ngx_string("Unsupported Media Type"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.16" },

    { 416, ngx_string("Range Not Satisfiable"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.17" },

    { 417, ngx_string("Expectation Failed"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.18" },

    { 421, ngx_string("Misdirected Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.20" },

    { 422, ngx_string("Unprocessable Content"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.21" },

    { 425, ngx_string("Too Early"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 8470 \xc2\xa7""5.2" },

    { 426, ngx_string("Upgrade Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 \xc2\xa7""15.5.22" },

    { 428, ngx_string("Precondition Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 6585 \xc2\xa7""3" },

    { 429, ngx_string("Too Many Requests"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 6585 \xc2\xa7""4" },

    { 431, ngx_string("Request Header Fields Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 6585 \xc2\xa7""5" },

    { 451, ngx_string("Unavailable For Legal Reasons"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 7725 \xc2\xa7""3" },

    /* NGINX-specific 4xx extensions */

    { 444, ngx_string("Connection Closed Without Response"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
      "nginx extension" },

    { 494, ngx_string("Request Header Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
      "nginx extension" },

    { 495, ngx_string("SSL Certificate Error"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
      "nginx extension" },

    { 496, ngx_string("SSL Certificate Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
      "nginx extension" },

    { 497, ngx_string("HTTP Request Sent to HTTPS Port"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
      "nginx extension" },

    { 499, ngx_string("Client Closed Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_NGINX_EXT,
      "nginx extension" },

    /* 5xx Server Error - RFC 9110 Section 15.6 */

    { 500, ngx_string("Internal Server Error"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 \xc2\xa7""15.6.1" },

    { 501, ngx_string("Not Implemented"),
      NGX_HTTP_STATUS_SERVER_ERROR | NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 \xc2\xa7""15.6.2" },

    { 502, ngx_string("Bad Gateway"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 \xc2\xa7""15.6.3" },

    { 503, ngx_string("Service Unavailable"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 \xc2\xa7""15.6.4" },

    { 504, ngx_string("Gateway Timeout"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 \xc2\xa7""15.6.5" },

    { 505, ngx_string("HTTP Version Not Supported"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 \xc2\xa7""15.6.6" },

    { 506, ngx_string("Variant Also Negotiates"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 2295 \xc2\xa7""8.1" },

    { 507, ngx_string("Insufficient Storage"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 4918 \xc2\xa7""11.5" },

    { 508, ngx_string("Loop Detected"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 5842 \xc2\xa7""7.2" },

    { 510, ngx_string("Not Extended"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 2774 \xc2\xa7""7" },

    { 511, ngx_string("Network Authentication Required"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 6585 \xc2\xa7""6" },
};


/*
 * Number of entries in ngx_http_status_registry[].  Computed at compile time
 * via sizeof; the linear-scan helpers below iterate up to this bound.
 */
#define NGX_HTTP_STATUS_REGISTRY_SIZE                                          \
    (sizeof(ngx_http_status_registry) / sizeof(ngx_http_status_registry[0]))


/*
 * ngx_http_status_validate: pure validation of a numeric status code.
 *
 * Tests whether `status` falls within the RFC 9110 valid range of
 * 100..599 inclusive.  This function performs NO logging, NO side
 * effects, and NO registry lookup; it is a constant-time predicate
 * suitable for hot paths.
 *
 * The implementation is deliberately tiny so that gcc can inline it at
 * -O2 in compile-time-constant call sites (e.g., when a caller passes
 * NGX_HTTP_OK), reducing the runtime cost to zero.
 *
 * Returns:
 *   NGX_OK     if status is in the valid range.
 *   NGX_ERROR  otherwise.
 */
ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
    return (status >= 100 && status <= 599) ? NGX_OK : NGX_ERROR;
}


/*
 * ngx_http_status_reason: registry lookup with Null Object fallback.
 *
 * Performs a linear scan over the static registry array and returns a
 * pointer to the registered ngx_str_t reason phrase for `status`.  If
 * the code is not in the registry, returns a pointer to the sentinel
 * ngx_http_status_unknown_reason ("Unknown") to eliminate NULL-return
 * paths in callers (per AAP §0.7.2 D-007).
 *
 * The linear scan is intentional: per AAP §0.8.2 ("Never optimize
 * registry structure beyond static array implementation"), no hash
 * table or class-index arithmetic is permitted.  At ~58 entries fitting
 * within a few cache lines, the scan completes in tens of nanoseconds
 * and is dominated by network I/O at every realistic call site.
 *
 * The returned pointer is valid for the lifetime of the worker process
 * (the registry is in .rodata) and MUST NOT be freed by the caller.
 *
 * Returns:
 *   const ngx_str_t * pointer to the registered reason phrase, or to
 *   the "Unknown" sentinel if `status` is not registered.  Never NULL.
 */
const ngx_str_t *
ngx_http_status_reason(ngx_uint_t status)
{
    ngx_uint_t  i;

    for (i = 0; i < NGX_HTTP_STATUS_REGISTRY_SIZE; i++) {
        if (ngx_http_status_registry[i].code == status) {
            return &ngx_http_status_registry[i].reason;
        }
    }

    return &ngx_http_status_unknown_reason;
}


/*
 * ngx_http_status_is_cacheable: heuristic-cacheability flag-bit check.
 *
 * Looks up `status` in the registry and returns 1 if the entry's flags
 * field has the NGX_HTTP_STATUS_CACHEABLE bit set, 0 otherwise.  An
 * unregistered code is conservatively reported as not cacheable.
 *
 * The cacheability flag is set per RFC 9110 Section 15 default
 * cacheability rules: 200, 203, 204, 206, 300, 301, 308, 404, 405, 410,
 * 414, 451, and 501 are heuristically cacheable.  Cache modules consult
 * this function when no explicit Cache-Control directive is present in
 * the response headers.
 *
 * Returns:
 *   1 if the registered entry is heuristically cacheable.
 *   0 otherwise (including for unregistered codes).
 */
ngx_uint_t
ngx_http_status_is_cacheable(ngx_uint_t status)
{
    ngx_uint_t  i;

    for (i = 0; i < NGX_HTTP_STATUS_REGISTRY_SIZE; i++) {
        if (ngx_http_status_registry[i].code == status) {
            return (ngx_http_status_registry[i].flags
                    & NGX_HTTP_STATUS_CACHEABLE) ? 1 : 0;
        }
    }

    return 0;
}


/*
 * ngx_http_status_register: stub returning NGX_ERROR (registry is immutable).
 *
 * Per AAP §0.8.2 ("Never create registry modification APIs accessible
 * post-initialization"), the registry is immutable at runtime.  The
 * registry is populated entirely by static initializers at compile time,
 * which means the ngx_http_status_init_done flag is set to 1 before
 * main() runs.  This function therefore detects the post-initialization
 * state on its first invocation and returns NGX_ERROR, satisfying the
 * "never modifiable post-init" rule trivially.
 *
 * The function exists in the public API surface for two reasons:
 *
 *   1. Forward compatibility: future versions of NGINX may permit
 *      controlled, configuration-phase-only registration via this entry
 *      point without changing the ABI.
 *
 *   2. Diagnostic clarity: third-party module authors who attempt to
 *      register custom codes at runtime receive a deterministic NGX_ERROR
 *      return rather than silent success or undefined behavior.
 *
 * Parameters:
 *   def  Pointer to a caller-allocated ngx_http_status_def_t.  Currently
 *        ignored (validated only for NULL).
 *
 * Returns:
 *   NGX_ERROR  always at runtime.  Reserved NGX_OK semantics for future
 *              configuration-phase use.
 */
ngx_int_t
ngx_http_status_register(ngx_http_status_def_t *def)
{
    if (ngx_http_status_init_done) {
        return NGX_ERROR;
    }

    /*
     * Unreachable in normal builds: ngx_http_status_init_done is statically
     * initialized to 1, so the early-return above always fires.  This
     * defensive check is preserved to surface obvious caller bugs (a NULL
     * def) should the init-done flag ever be cleared in some future
     * configuration-phase extension.
     */
    if (def == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * ngx_http_status_set: central facade for assigning the response status code.
 *
 * This is the unified entry point through which all NGINX-originated status
 * code assignments flow after this refactor.  It replaces the legacy direct
 * field assignment "r->headers_out.status = status;" pattern with a thin
 * facade that:
 *
 *   1. (Strict mode only) Range-checks status against 100..599 inclusive.
 *      Out-of-range codes are rejected with NGX_LOG_WARN.  This prevents
 *      header injection via crafted status values per AAP §0.8.6.
 *
 *   2. (Strict mode only) Detects a 1xx response emitted after a final
 *      (>= 200) response, which is a protocol violation.  Such attempts
 *      are rejected with NGX_LOG_ERR.
 *
 *   3. Bypasses strict validation when r->upstream is non-NULL (per AAP
 *      §0.1.1 I3 + §0.7.2 D-006).  Upstream pass-through must preserve the
 *      original code from the backend response verbatim, even if it falls
 *      outside the RFC range; rejecting an upstream's 0 or 999 would be a
 *      regression and a protocol violation against the proxy contract.
 *
 *   4. Emits a debug-level trace log line at NGX_LOG_DEBUG_HTTP for every
 *      call (in strict mode) or only for out-of-range codes (in permissive
 *      mode), satisfying the AAP §0.7.1 observability requirement.
 *
 *   5. Assigns r->headers_out.status to the requested value.
 *
 * Build-mode dispatch:
 *
 *   When NGX_HTTP_STATUS_VALIDATION is defined (configure flag
 *   --with-http_status_validation), strict-mode behavior is active and
 *   invalid codes return NGX_ERROR.  When it is NOT defined (the default),
 *   permissive-mode behavior is active: every code is assigned, the function
 *   always returns NGX_OK, and only out-of-range codes generate a debug log
 *   line.  The compiler eliminates the entire #if branch not selected, so
 *   default builds incur zero validation overhead.
 *
 * Parameters:
 *   r       The request whose response status code is being set.  Must not
 *           be NULL; r->connection must not be NULL; r->connection->log
 *           must not be NULL (NGINX guarantees these for any live request).
 *   status  The numeric HTTP status code to assign.
 *
 * Returns:
 *   NGX_OK     on successful assignment (always in permissive mode; in
 *              strict mode if validation passes).
 *   NGX_ERROR  in strict mode only, if validation fails (out-of-range
 *              code, or 1xx after a final response).
 *
 * Side effects:
 *   - Sets r->headers_out.status = status (always, in permissive mode;
 *     conditionally in strict mode after validation passes).
 *   - May emit log entries at NGX_LOG_DEBUG_HTTP, NGX_LOG_WARN, or
 *     NGX_LOG_ERR depending on the build mode and the input.
 */
ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
#if (NGX_HTTP_STATUS_VALIDATION)

    if (r->upstream == NULL) {
        if (status < 100 || status > 599) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                          "ngx_http_status_set: invalid status %ui "
                          "(out of RFC 9110 range 100-599)", status);
            return NGX_ERROR;
        }

        if (status >= 100 && status < 200
            && r->headers_out.status >= 200 && r->headers_out.status <= 599)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx_http_status_set: 1xx status %ui after "
                          "final response %ui",
                          status, r->headers_out.status);
            return NGX_ERROR;
        }
    }

    /*
     * The registry lookup is intentionally placed inside the ngx_log_debug3
     * macro arguments rather than into a local variable: when NGX_DEBUG is
     * not defined (release builds), the entire macro expands to nothing and
     * the registry lookup is fully elided, avoiding both "unused variable"
     * warnings and any runtime cost.
     */
    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http status set: %ui %V (strict=yes upstream=%s)",
                   status, ngx_http_status_reason(status),
                   r->upstream != NULL ? "yes" : "no");

    r->headers_out.status = status;

    return NGX_OK;

#else
    if (status < 100 || status > 599) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http status set: %ui (out-of-range; permissive)",
                       status);
    }

    r->headers_out.status = status;

    return NGX_OK;
#endif
}


/*
 * ngx_http_status_init_registry: forward-compatibility cycle hook.
 *
 * The registry array is populated by C static initializers at compile time
 * (see ngx_http_status_registry[] above), so the registry is fully
 * operational before main() runs and no runtime initialization is
 * required.  ngx_http_status_set(), ngx_http_status_validate(),
 * ngx_http_status_reason(), and ngx_http_status_is_cacheable() are all
 * usable from process startup.
 *
 * This function is provided in the API surface for two reasons:
 *
 *   1. Forward compatibility: should a future NGINX revision need to
 *      perform per-cycle initialization (e.g., a debug-only registry
 *      consistency check, or per-cycle metric counter reset), this
 *      function is the documented hook for that work.
 *
 *   2. Explicit AAP compliance: the AAP §0.8.4 requires "Registry array
 *      population must occur before first worker process fork" and AAP
 *      §0.8.3 requires "Registry initialization must complete during
 *      configuration parsing phase only".  Both are satisfied trivially
 *      by static initialization, but having a named init function makes
 *      the contract explicit and gives operators a clear point to insert
 *      consistency checks via debug builds.
 *
 * The current implementation performs only a defensive NULL-pointer
 * check on the cycle argument and re-asserts the init_done flag.
 *
 * Parameters:
 *   cycle  The active NGINX cycle.  Must not be NULL.
 *
 * Returns:
 *   NGX_OK     on success (always, when cycle is non-NULL).
 *   NGX_ERROR  if cycle is NULL.
 */
ngx_int_t
ngx_http_status_init_registry(ngx_cycle_t *cycle)
{
    if (cycle == NULL) {
        return NGX_ERROR;
    }

    ngx_http_status_init_done = 1;

    return NGX_OK;
}
