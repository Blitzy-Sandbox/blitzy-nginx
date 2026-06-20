# HTTP Status Code Registry & API

The `ngx_http_status_*` subsystem is the single source of truth for HTTP
status-code identity in nginx. This page is the public reference for the
registry descriptor type, the status-class / cacheability flags, the five-call
API, the full set of registered codes, the cacheable-by-default metadata, and
the optional `--with-http_status_validation` build mode.

The API is declared in `src/http/ngx_http_status.h` and implemented in
`src/http/ngx_http_status.c`. Because `src/http/ngx_http.h` includes
`ngx_http_status.h`, the API is visible in virtually every HTTP translation
unit without an extra include.

## Overview

The registry is the **single source of truth** for a status code's identity:
its numeric value, its reason phrase, its class / cacheability metadata, and the
RFC 9110 section that defines it. A single authoritative model replaces the
three artifacts that previously each declared an overlapping subset of the same
code space:

1. the `NGX_HTTP_*` `#define` block in `src/http/ngx_http_request.h`;
2. the header filter's `ngx_http_status_lines[]` reason-phrase table; and
3. the special-response `ngx_http_error_pages[]` table.

These three tables were kept in sync only by developer discipline, and that
discipline had already failed: the same offset macro `NGX_HTTP_LAST_2XX` was
defined as `207` in the header filter and as `202` in the special-response
module. Routing both the reason-phrase lookup and the error-page lookup through
one registry eliminates that divergence at its source.

In the *before* state, the three tables were independent C objects that no
compiler cross-checked, so they could — and did — drift apart. In the *after*
state, the registry is the single source of truth for which codes exist and for
each code's class and cacheability metadata, and both consumers now derive their
behavior from it:

- The header filter's reason-phrase lookup is routed entirely through
  `ngx_http_status_reason()`.
- The special-response error-page lookup is **registry-fed**: a built-in body is
  rendered only for a registry-valid code (`ngx_http_status_validate()`), the
  body is selected by that status code (no per-class offset arithmetic), and the
  client/server-error class that drives MSIE/Chrome padding is read from the
  registry's authoritative flags (`ngx_http_status_flags()`). The error-page
  **bodies** themselves remain as the unchanged `static` byte arrays in
  `ngx_http_special_response.c`; the registry intentionally stores metadata, not
  body content, so the wire output is byte-identical to previous releases.

Implementation properties:

- Backed by a **single `static const` array** of `ngx_http_status_def_t`.
- **O(1) class-offset lookup** — a code is resolved to its array slot by
  range arithmetic, not a scan.
- **Read-only after initialization** — the core array is `static const` and is
  never written at runtime.
- **Under 1 KB per worker.** The array lives in the binary's `.rodata`
  segment, so it is **shared across all worker processes**; copy-on-write never
  triggers because the memory is never written, making the per-worker
  incremental footprint effectively zero.

**Backward compatibility.** The wire output — status lines and error-page
bodies — is **byte-identical** to previous releases, and the legacy
`NGX_HTTP_*` constants remain valid and continue to name the same numeric
values. The refactor changes how a status code is looked up internally, not
what nginx sends on the wire.

## The registry descriptor

`ngx_http_status_def_t` (declared in `src/http/ngx_http_status.h`) captures the
full identity of one status code:

```c
typedef struct {
    ngx_uint_t   code;        /* e.g. 404 */
    ngx_str_t    reason;      /* full status line, e.g. "200 OK" */
    ngx_uint_t   flags;       /* class + cacheability bitmask */
    const char  *rfc_section; /* e.g. "15.5.5" */
} ngx_http_status_def_t;
```

Fields:

- **`code`** — the numeric HTTP status code (e.g. `404`).
- **`reason`** — the **full status-line text, including the numeric prefix**
  (e.g. `"200 OK"`, `"404 Not Found"`). The numeric prefix is part of the
  stored string for **byte-parity** with the legacy `ngx_http_status_lines[]`
  table. **Placeholder / numeric-only codes store an empty `ngx_str_t`**
  (`ngx_null_string`), and callers render those as a bare numeric status line.
- **`flags`** — a bitmask combining the status-class flag and the cacheability
  flag (see [Flag bits](#flag-bits)).
- **`rfc_section`** — an **informational** pointer to the RFC 9110 §15
  subsection that defines the code (e.g. `"15.5.5"`). It may be `NULL` for codes
  without a clean §15 mapping (for example `102`, `103`, and the nginx
  extension codes). This field is never used in wire output.

## Flag bits

The `flags` field combines exactly one status-class flag with the optional
cacheability flag. The four `NGX_HTTP_STATUS_*` macros are defined in
`src/http/ngx_http_status.h`:

```c
#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008
```

| Flag | Value | Meaning |
|------|-------|---------|
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | Heuristically cacheable by default. **Metadata only — introduces no caching behavior**; nginx's cache layer is unchanged. |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0002` | 4xx responses (and the nginx 49x extension codes). |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0004` | 5xx responses. |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0008` | 1xx responses. |

A 2xx or 3xx success/redirect code carries no class flag; it has either `0` or
`NGX_HTTP_STATUS_CACHEABLE` in its `flags`. The cacheability flag may be
combined with a class flag (for example `404` carries
`NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE`).

## API

All functions are declared in `src/http/ngx_http_status.h` and implemented in
`src/http/ngx_http_status.c` (except `ngx_http_status_set()` in the default
build, which is an inline defined in the header — see below). Functions follow
the nginx convention of returning `ngx_int_t` with `NGX_OK` / `NGX_ERROR`, or a
small scalar for predicate-style accessors.

### `ngx_http_status_set`

The single status-assignment chokepoint. Use it in place of writing
`r->headers_out.status` directly.

```c
ngx_int_t  ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
```

- **Default build (validation OFF):** a zero-overhead `static ngx_inline`
  function defined in `ngx_http_status.h` that compiles to
  `r->headers_out.status = status; return NGX_OK;`. Constant arguments such as
  `NGX_HTTP_OK` constant-fold to `200`, so there is no measurable overhead
  (under 10 CPU cycles, and behavior byte-identical to the legacy direct field
  write).
- **Validation build (ON, `--with-http_status_validation`):** a real function
  that validates the code. It **bypasses validation when `r->upstream` is set**
  so proxied/upstream responses are passed through unchanged. It returns
  `NGX_OK` on success and **`NGX_ERROR` on an invalid (out-of-range) code**; by
  convention the caller then logs an `NGX_LOG_ERR` and falls back to
  `NGX_HTTP_INTERNAL_SERVER_ERROR`.

Returns `NGX_OK` when the status was assigned, `NGX_ERROR` when a locally
generated code was rejected (validation build only).

```c
/* OLD: r->headers_out.status = NGX_HTTP_NOT_FOUND; */
if (ngx_http_status_set(r, NGX_HTTP_NOT_FOUND) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid HTTP status %ui", (ngx_uint_t) NGX_HTTP_NOT_FOUND);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

### `ngx_http_status_validate`

RFC 9110 structural conformance check.

```c
ngx_int_t  ngx_http_status_validate(ngx_uint_t status);
```

Returns `NGX_OK` when `100 <= status <= 599` — the RFC-defined three-digit
class range, which accepts every standard code and the nginx extensions
(`444`, `494`–`499`) — otherwise `NGX_ERROR`. A code that is merely unknown but
in range is still structurally valid.

```c
if (ngx_http_status_validate(status) != NGX_OK) {
    /* status is outside the valid 100..599 range */
}
```

### `ngx_http_status_reason`

Reason-phrase accessor. It **replaces** the header filter's
`ngx_http_status_lines[]` offset arithmetic.

```c
ngx_str_t  ngx_http_status_reason(ngx_uint_t status);
```

Returns the **full status-line text** (e.g. `"200 OK"`, `"404 Not Found"`) for
codes that carry a reason phrase, and an **empty `ngx_str_t`** (`.len == 0`)
for **numeric-only, unknown, 1xx, and nginx-extension** codes — those emit a
numeric-only status line such as `"203 "`. The returned `.data` points into
`static const` storage, so the value lives for the lifetime of the process and
is safe to copy and to outlive the request.

```c
ngx_str_t  reason;

reason = ngx_http_status_reason(r->headers_out.status);
if (reason.len) {
    /* e.g. "200 OK" -> "HTTP/1.1 200 OK\r\n" */
} else {
    /* numeric-only status line, e.g. "HTTP/1.1 203 \r\n" */
}
```

### `ngx_http_status_register`

Extensibility seam for third-party status codes.

```c
ngx_int_t  ngx_http_status_register(const ngx_http_status_def_t *def);
```

Registers an additional code descriptor. It is intended to be called **before
worker fork** so that every worker inherits it. The registration table has a
bounded capacity of **32** codes. The function returns `NGX_ERROR` on a `NULL`
argument, once the registry has been frozen by `ngx_http_status_init()`, or on
overflow (capacity exceeded); otherwise it copies the descriptor into the table
and returns `NGX_OK`. The registry is read-only after initialization.

The descriptor below is a **template**, not a concrete new code: substitute the
status code your module emits, its reason phrase, the appropriate
`NGX_HTTP_STATUS_*` class/cacheability flags, and an optional RFC-section string.
This API only stores descriptors a caller supplies; it does not define any status
codes beyond the core registry on its own.

```c
static const ngx_http_status_def_t  my_code = {
    /* code        */ MY_STATUS_CODE,                /* the code your module emits */
    /* reason      */ ngx_string("<reason phrase>"), /* its reason phrase          */
    /* flags       */ NGX_HTTP_STATUS_CLIENT_ERROR,  /* class / cacheability bits  */
    /* rfc_section */ NULL                           /* optional, e.g. "15.5.1"    */
};

if (ngx_http_status_register(&my_code) != NGX_OK) {
    /* NULL descriptor, registry frozen, or full (>= 32 registered) */
}
```

### `ngx_http_status_is_cacheable`

Cacheability flag test.

```c
ngx_uint_t  ngx_http_status_is_cacheable(ngx_uint_t status);
```

Returns `1` if the code is registered and its `flags` include
`NGX_HTTP_STATUS_CACHEABLE`, otherwise `0`. This is a **metadata query only** —
it does not change caching behavior.

```c
if (ngx_http_status_is_cacheable(r->headers_out.status)) {
    /* code is heuristically cacheable by default (metadata only) */
}
```

### `ngx_http_status_init`

Pre-fork initialization hook.

```c
ngx_int_t  ngx_http_status_init(void);
```

Idempotent, allocation-free, one-time initialization. It is called from the
HTTP header-filter postconfiguration hook (`ngx_http_header_filter_init` in
`src/http/ngx_http_header_filter_module.c`), which runs in the master process
during HTTP configuration — that is, **before worker fork** — so every worker
inherits the read-only registry and no shared-memory migration is required for a
graceful binary upgrade. Calling it freezes the registration table (see
`ngx_http_status_register`). Returns `NGX_OK`.

## Registered codes

The core registry covers the RFC 9110 §15 codes targeted by the refactor plus
the nginx extension codes. The standard ranges are:

- **1xx informational:** `100`–`103`
- **2xx success:** `200`–`206`
- **3xx redirection:** `300`–`308`
- **4xx client error:** `400`–`429` (stored contiguously; the gap codes carry
  an empty reason so the class can be indexed in O(1))
- **5xx server error:** `500`–`507`

The **nginx extension codes** are `444`, `494`, `495`, `496`, `497`, `498`, and
`499`. They **emit numeric-only status lines** and their existing security
behaviors are unchanged and live in the callers, not in the registry:

- `444` closes the connection without sending a response.
- `494`, `495`, `496`, and `497` are rewritten to `400` on the wire by the
  special-response module.
- `498` maps to the `404` error page.
- `499` is logged when the client closed the connection.

In the tables below, a reason of _(numeric-only)_ means the registry stores an
empty `ngx_str_t` for that code (`ngx_http_status_reason()` returns `.len == 0`)
and nginx emits a bare numeric status line. "Cacheable" reflects the
`NGX_HTTP_STATUS_CACHEABLE` metadata flag only (see
[Cacheable-by-default codes](#cacheable-by-default-codes)). The RFC 9110 column
is informational and is not used in wire output (`—` means no clean §15
mapping).

### 1xx — Informational

| Code | Reason phrase (status-line text) | Cacheable | RFC 9110 |
|------|----------------------------------|-----------|----------|
| 100 | _(numeric-only)_ | — | §15.2.1 |
| 101 | _(numeric-only)_ | — | §15.2.2 |
| 102 | _(numeric-only)_ | — | — |
| 103 | _(numeric-only)_ | — | — |

### 2xx — Success

| Code | Reason phrase (status-line text) | Cacheable | RFC 9110 |
|------|----------------------------------|-----------|----------|
| 200 | `"200 OK"` | Yes | §15.3.1 |
| 201 | `"201 Created"` | — | §15.3.2 |
| 202 | `"202 Accepted"` | — | §15.3.3 |
| 203 | _(numeric-only)_ | Yes | §15.3.4 |
| 204 | `"204 No Content"` | Yes | §15.3.5 |
| 205 | _(numeric-only)_ | — | §15.3.6 |
| 206 | `"206 Partial Content"` | Yes | §15.3.7 |

### 3xx — Redirection

| Code | Reason phrase (status-line text) | Cacheable | RFC 9110 |
|------|----------------------------------|-----------|----------|
| 300 | _(numeric-only)_ | Yes | §15.4.1 |
| 301 | `"301 Moved Permanently"` | Yes | §15.4.2 |
| 302 | `"302 Moved Temporarily"` | — | §15.4.3 |
| 303 | `"303 See Other"` | — | §15.4.4 |
| 304 | `"304 Not Modified"` | — | §15.4.5 |
| 305 | _(numeric-only)_ | — | §15.4.6 |
| 306 | _(numeric-only)_ | — | — |
| 307 | `"307 Temporary Redirect"` | — | §15.4.8 |
| 308 | `"308 Permanent Redirect"` | Yes | §15.4.9 |

### 4xx — Client error

| Code | Reason phrase (status-line text) | Cacheable | RFC 9110 |
|------|----------------------------------|-----------|----------|
| 400 | `"400 Bad Request"` | — | §15.5.1 |
| 401 | `"401 Unauthorized"` | — | §15.5.2 |
| 402 | `"402 Payment Required"` | — | §15.5.3 |
| 403 | `"403 Forbidden"` | — | §15.5.4 |
| 404 | `"404 Not Found"` | Yes | §15.5.5 |
| 405 | `"405 Not Allowed"` | Yes | §15.5.6 |
| 406 | `"406 Not Acceptable"` | — | §15.5.7 |
| 407 | _(numeric-only)_ | — | §15.5.8 |
| 408 | `"408 Request Time-out"` | — | §15.5.9 |
| 409 | `"409 Conflict"` | — | §15.5.10 |
| 410 | `"410 Gone"` | Yes | §15.5.11 |
| 411 | `"411 Length Required"` | — | §15.5.12 |
| 412 | `"412 Precondition Failed"` | — | §15.5.13 |
| 413 | `"413 Request Entity Too Large"` | — | §15.5.14 |
| 414 | `"414 Request-URI Too Large"` | Yes | §15.5.15 |
| 415 | `"415 Unsupported Media Type"` | — | §15.5.16 |
| 416 | `"416 Requested Range Not Satisfiable"` | — | §15.5.17 |
| 417 | _(numeric-only)_ | — | §15.5.18 |
| 418 | _(numeric-only)_ | — | — |
| 419 | _(numeric-only)_ | — | — |
| 420 | _(numeric-only)_ | — | — |
| 421 | `"421 Misdirected Request"` | — | §15.5.20 |
| 422 | _(numeric-only)_ | — | §15.5.21 |
| 423 | _(numeric-only)_ | — | — |
| 424 | _(numeric-only)_ | — | — |
| 425 | _(numeric-only)_ | — | — |
| 426 | _(numeric-only)_ | — | §15.5.22 |
| 427 | _(numeric-only)_ | — | — |
| 428 | _(numeric-only)_ | — | — |
| 429 | `"429 Too Many Requests"` | — | — |

### 5xx — Server error

| Code | Reason phrase (status-line text) | Cacheable | RFC 9110 |
|------|----------------------------------|-----------|----------|
| 500 | `"500 Internal Server Error"` | — | §15.6.1 |
| 501 | `"501 Not Implemented"` | Yes | §15.6.2 |
| 502 | `"502 Bad Gateway"` | — | §15.6.3 |
| 503 | `"503 Service Temporarily Unavailable"` | — | §15.6.4 |
| 504 | `"504 Gateway Time-out"` | — | §15.6.5 |
| 505 | `"505 HTTP Version Not Supported"` | — | §15.6.6 |
| 506 | _(numeric-only)_ | — | — |
| 507 | `"507 Insufficient Storage"` | — | — |

### nginx extension codes

| Code | Reason phrase (status-line text) | Cacheable |
|------|----------------------------------|-----------|
| 444 | _(numeric-only)_ | — |
| 494 | _(numeric-only)_ | — |
| 495 | _(numeric-only)_ | — |
| 496 | _(numeric-only)_ | — |
| 497 | _(numeric-only)_ | — |
| 498 | _(numeric-only)_ | — |
| 499 | _(numeric-only)_ | — |

## Cacheable-by-default codes

Exactly these **12** codes carry the `NGX_HTTP_STATUS_CACHEABLE` flag:

```text
200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501
```

This is **metadata only.** The flag exposes the *heuristically cacheable by
default* property — consistent with the heuristic-freshness reasoning described
in RFC 9111 — **without introducing any new caching behavior**. nginx's
existing cache layer is unchanged; `ngx_http_status_is_cacheable()` merely reads
this flag for callers that want to reason about a code's default cacheability.

## Build mode: `--with-http_status_validation`

RFC 9110 validation is **off by default**.

- **Disabled (default):** `ngx_http_status_set()` is the zero-overhead
  `static ngx_inline` defined in `ngx_http_status.h`. It is identical to the
  legacy direct field write (`r->headers_out.status = status;`) and adds **zero
  overhead**.
- **Enabled (`--with-http_status_validation`):** the build defines
  `NGX_HTTP_STATUS_VALIDATION 1`, and `ngx_http_status_set()` becomes a real
  validating function. A locally generated out-of-range code returns
  `NGX_ERROR`; by convention the caller logs an `NGX_LOG_ERR` and falls back to
  `NGX_HTTP_INTERNAL_SERVER_ERROR`.

In **either** mode, **proxied / upstream responses bypass strict validation**
and are passed through unchanged, because `ngx_http_status_set()`
short-circuits when `r->upstream` is set.

```bash
# default (validation disabled, zero overhead)
auto/configure && make

# enable RFC 9110 section 15 validation
auto/configure --with-http_status_validation && make
```

## See also

- `src/http/ngx_http_status.h` — the descriptor type, flag bits, API
  prototypes, the default-build inline setter, and the init hook.
- `src/http/ngx_http_status.c` — the static registry array (the authoritative
  source of every reason phrase, flag, and RFC section) and the implementations
  of `ngx_http_status_validate()`, `ngx_http_status_reason()`,
  `ngx_http_status_register()`, `ngx_http_status_is_cacheable()`,
  `ngx_http_status_set()` (validation build), and `ngx_http_status_init()`.
- `src/http/ngx_http.h` — includes `ngx_http_status.h`, making the API visible
  across the HTTP subsystem.
