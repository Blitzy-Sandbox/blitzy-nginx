# HTTP Status Code Registry & API

The status-code registry is the centralized model for HTTP status codes in
NGINX. It supersedes the previously scattered, hand-synchronized status-code
declarations with a single authoritative source and a small API that mediates
status assignment and lookup.

This page is the public reference for the `ngx_http_status_*` subsystem. The
authoritative definitions live in `src/http/ngx_http_status.h` (types, flag
bits, and prototypes) and `src/http/ngx_http_status.c` (the registry array and
the API implementation); the API is made visible to the rest of the HTTP code
through `src/http/ngx_http.h`.

## Overview

The registry is the **single source of truth** for the identity of every HTTP
status code that NGINX emits: its numeric value, its reason phrase, its
RFC 9110 class and cacheability metadata, and the RFC section that defines it.

Before this subsystem, the same logical fact — "status code *N* exists and has
reason phrase *R*" — was duplicated across **three independent artifacts** that
no compiler cross-checked:

1. The `NGX_HTTP_*` `#define` constants in `src/http/ngx_http_request.h`.
2. The header filter's reason-phrase table, `ngx_http_status_lines[]`, in
   `src/http/ngx_http_header_filter_module.c`.
3. The special-response error-page table, `ngx_http_error_pages[]`, in
   `src/http/ngx_http_special_response.c`.

These tables were kept in sync only by developer discipline, and that discipline
had already failed: the `NGX_HTTP_LAST_2XX` offset macro carried two different
values in two files — **207** in the header filter versus **202** in the
special-response module. The registry consolidates all three artifacts behind
one model and eliminates that divergence.

### Implementation properties

- **Single `static const` array.** Every code is one `ngx_http_status_def_t`
  entry in a single array defined in `src/http/ngx_http_status.c`.
- **O(1) class-offset lookup.** A code is resolved to its array index by a
  single range check and a subtraction per class region; there is no scan of the
  core table on the common path.
- **Read-only after initialization.** The registry is finalized before the
  worker fork and is never written afterwards.
- **Under 1 KB per worker.** Because the array is `static const`, it lives in the
  binary's `.rodata` segment and is therefore **shared across all worker
  processes**. Copy-on-write never triggers, because the memory is never written.

### Visibility

The API is declared in `src/http/ngx_http_status.h` and is made visible wherever
`src/http/ngx_http.h` is included — which is virtually every HTTP translation
unit — because `ngx_http.h` adds `#include <ngx_http_status.h>`.

### Backward compatibility

Wire output — status lines and error-page bodies — is **byte-identical** to
previous releases. The legacy `NGX_HTTP_*` constants remain valid and may
continue to be passed as the `status` argument to the API (for example,
`ngx_http_status_set(r, NGX_HTTP_OK)`).

## The registry descriptor

Each status code is described by one `ngx_http_status_def_t` value, declared in
`src/http/ngx_http_status.h`:

```c
typedef struct {
    ngx_uint_t   code;        /* e.g. 404 */
    ngx_str_t    reason;      /* full status-line text, e.g. "404 Not Found";
                                 empty ngx_str_t for numeric-only codes */
    ngx_uint_t   flags;       /* class + cacheability bitmask */
    const char  *rfc_section; /* e.g. "15.5.5" (informational) */
} ngx_http_status_def_t;
```

Fields:

- **`code`** — the numeric HTTP status code (e.g. `404`).
- **`reason`** — the **full status-line text, including the numeric prefix**
  (e.g. `"200 OK"`, `"404 Not Found"`). Storing the complete text — rather than
  just the phrase — preserves **byte-parity** with the legacy
  `ngx_http_status_lines[]` table. **Placeholder / numeric-only codes store an
  empty `ngx_str_t`** (`ngx_null_string`).
- **`flags`** — a bitmask combining the class flag and the cacheability flag
  (see [Flag bits](#flag-bits)).
- **`rfc_section`** — an **informational** pointer to the RFC 9110 §15
  subsection that defines the code (e.g. `"15.5.5"`). It may be `NULL` for codes
  without a clean §15 mapping (for example 102, 103, and the nginx extension
  codes). It is **not** used in wire output.

## Flag bits

The `flags` field is a bitmask built from the following macros, declared in
`src/http/ngx_http_status.h`:

```c
#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0002
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0004
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0008
```

| Macro | Value | Meaning |
|---|---|---|
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | Heuristically cacheable by default. **Metadata only** — introduces no caching behavior; nginx's cache layer is unchanged. |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0002` | 4xx responses (and the nginx 49x extension codes). |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0004` | 5xx responses. |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0008` | 1xx responses. |

A descriptor may combine the class flag with the cacheability flag. For example,
`404` carries `NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE`.

## API

All functions are declared in `src/http/ngx_http_status.h`. Functions return
`ngx_int_t` (`NGX_OK` / `NGX_ERROR`) unless noted otherwise, following the
surrounding nginx conventions.

### `ngx_http_status_set`

The assignment chokepoint — the single function through which response status is
set.

```c
ngx_int_t  ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
```

- **Default build (validation off).** `ngx_http_status_set()` is a zero-overhead
  `static ngx_inline` function (defined in `ngx_http_status.h`) that compiles to
  `r->headers_out.status = status; return NGX_OK;`. Constant arguments such as
  `NGX_HTTP_OK` constant-fold to `200`, so there is no measurable overhead
  (< 10 CPU cycles; behavior byte-identical to the legacy direct field write).
  In a `--with-debug` build it additionally emits a single `NGX_LOG_DEBUG_HTTP`
  status-set trace; that call expands to nothing in production builds.
- **Validation build (on, `--with-http_status_validation`).** The code is
  validated before it is stored. Validation is **bypassed when `r->upstream` is
  set**, so proxied/upstream responses are passed through unchanged. The function
  returns `NGX_OK` on success and **`NGX_ERROR` on an invalid code**; on
  `NGX_ERROR` the caller is expected to log an `NGX_LOG_ERR` and fall back to
  `NGX_HTTP_INTERNAL_SERVER_ERROR`.

**Returns:** `NGX_OK` when the status was assigned; `NGX_ERROR` (validation build
only) when the code is invalid.

**Example** — the standard module migration pattern:

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

Returns `NGX_OK` when `100 <= status <= 599` — the RFC-defined three-digit class
range, which accepts every standard code as well as the nginx extensions (444
and 494–499) — and `NGX_ERROR` otherwise. A code that is in range but not present
in the registry is still structurally valid.

**Returns:** `NGX_OK` for an in-range code; `NGX_ERROR` otherwise.

**Example:**

```c
if (ngx_http_status_validate(status) != NGX_OK) {
    /* status is outside the valid 100..599 range */
}
```

### `ngx_http_status_reason`

Reason-phrase accessor that **replaces** the header filter's
`ngx_http_status_lines[]` offset arithmetic.

```c
ngx_str_t  ngx_http_status_reason(ngx_uint_t status);
```

Returns the **full status-line text** (e.g. `"200 OK"`, `"404 Not Found"`) for
codes that carry a reason phrase, and an **empty `ngx_str_t`** (`.len == 0`) for
**numeric-only, unknown, 1xx, and nginx-extension** codes. An empty result tells
the header filter to emit a numeric-only status line (for example
`"HTTP/1.1 203 \r\n"`), preserving byte-for-byte wire output. The returned
`.data` points into `static const` storage, so the value is safe to copy and
outlives the request.

**Returns:** an `ngx_str_t` holding the full status-line text, or an empty
`ngx_str_t` for numeric-only / unknown codes.

**Example:**

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
the worker fork**, so that every worker inherits the registered code. The
descriptor is copied by value into a bounded table with a capacity of **32**
registered codes. The function returns `NGX_ERROR` when the argument is `NULL`,
when the table is full (capacity exceeded), or when the registry has already been
finalized by `ngx_http_status_init()` (the registry is read-only after
initialization); otherwise it returns `NGX_OK`.

**Returns:** `NGX_OK` on success; `NGX_ERROR` for a `NULL` descriptor, on
overflow (≥ 32 registered), or after the registry has been finalized.

**Example:**

```c
static const ngx_http_status_def_t  my_code = {
    218, ngx_string("218 This Is Fine"), 0, NULL
};

if (ngx_http_status_register(&my_code) != NGX_OK) {
    /* NULL descriptor, registry full (>= 32 registered), or already finalized */
}
```

### `ngx_http_status_is_cacheable`

Cacheability flag test.

```c
ngx_uint_t  ngx_http_status_is_cacheable(ngx_uint_t status);
```

Returns `1` if the code is registered and its flags include
`NGX_HTTP_STATUS_CACHEABLE`, and `0` otherwise (including for unknown codes).
This is a **metadata query only** — it does not change caching behavior.

**Returns:** `1` for a cacheable-by-default code; `0` otherwise.

**Example:**

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

Idempotent, allocation-free, one-time initialization called from
`src/http/ngx_http_request.c` **before the worker fork**, so that every worker
inherits the same read-only registry (no shared-memory migration is needed for a
graceful binary upgrade). After it returns, the registry is finalized and
`ngx_http_status_register()` rejects further mutation.

**Returns:** `NGX_OK`.


## Registered codes

The core registry covers the following standard RFC 9110 §15 ranges plus the
nginx extension codes:

- **Standard ranges:** `100–103`, `200–206`, `300–308`, `400–429` (contiguous;
  the gap codes carry an empty reason so the region can be indexed in O(1)), and
  `500–507`.
- **nginx extension codes:** `444`, `494`, `495`, `496`, `497`, `498`, `499`.
  These **emit numeric-only status lines** and **preserve their existing security
  behaviors**. In particular, 494/495/496/497 are rewritten to `400` on the wire
  by the special-response module, `444` closes the connection without any
  response, and `499` is logged when the client closed the connection before the
  response was sent.

In the tables below, the **reason** column shows the exact wire text (the value
of the descriptor's `reason` field); `*(numeric-only)*` marks a code stored with
an empty `ngx_str_t`, which is emitted as a numeric-only status line. The
**flags** column omits the `NGX_HTTP_STATUS_` prefix for brevity (so `CACHEABLE`
means `NGX_HTTP_STATUS_CACHEABLE`, and so on); `—` means no flags are set.

### 1xx — Informational

All 1xx codes are stored numeric-only.

| Code | Reason | Flags |
|---|---|---|
| 100 | *(numeric-only)* | INFORMATIONAL |
| 101 | *(numeric-only)* | INFORMATIONAL |
| 102 | *(numeric-only)* | INFORMATIONAL |
| 103 | *(numeric-only)* | INFORMATIONAL |

### 2xx — Successful

| Code | Reason | Flags |
|---|---|---|
| 200 | `200 OK` | CACHEABLE |
| 201 | `201 Created` | — |
| 202 | `202 Accepted` | — |
| 203 | *(numeric-only)* | CACHEABLE |
| 204 | `204 No Content` | CACHEABLE |
| 205 | *(numeric-only)* | — |
| 206 | `206 Partial Content` | CACHEABLE |

### 3xx — Redirection

| Code | Reason | Flags |
|---|---|---|
| 300 | *(numeric-only)* | CACHEABLE |
| 301 | `301 Moved Permanently` | CACHEABLE |
| 302 | `302 Moved Temporarily` | — |
| 303 | `303 See Other` | — |
| 304 | `304 Not Modified` | — |
| 305 | *(numeric-only)* | — |
| 306 | *(numeric-only)* | — |
| 307 | `307 Temporary Redirect` | — |
| 308 | `308 Permanent Redirect` | CACHEABLE |

### 4xx — Client error

Every 4xx code carries `CLIENT_ERROR`. The gap codes (407, 417–420, 422–428) are
stored numeric-only but are still part of the contiguous `400–429` region.

| Code | Reason | Flags |
|---|---|---|
| 400 | `400 Bad Request` | CLIENT_ERROR |
| 401 | `401 Unauthorized` | CLIENT_ERROR |
| 402 | `402 Payment Required` | CLIENT_ERROR |
| 403 | `403 Forbidden` | CLIENT_ERROR |
| 404 | `404 Not Found` | CLIENT_ERROR + CACHEABLE |
| 405 | `405 Not Allowed` | CLIENT_ERROR + CACHEABLE |
| 406 | `406 Not Acceptable` | CLIENT_ERROR |
| 407 | *(numeric-only)* | CLIENT_ERROR |
| 408 | `408 Request Time-out` | CLIENT_ERROR |
| 409 | `409 Conflict` | CLIENT_ERROR |
| 410 | `410 Gone` | CLIENT_ERROR + CACHEABLE |
| 411 | `411 Length Required` | CLIENT_ERROR |
| 412 | `412 Precondition Failed` | CLIENT_ERROR |
| 413 | `413 Request Entity Too Large` | CLIENT_ERROR |
| 414 | `414 Request-URI Too Large` | CLIENT_ERROR + CACHEABLE |
| 415 | `415 Unsupported Media Type` | CLIENT_ERROR |
| 416 | `416 Requested Range Not Satisfiable` | CLIENT_ERROR |
| 417 | *(numeric-only)* | CLIENT_ERROR |
| 418 | *(numeric-only)* | CLIENT_ERROR |
| 419 | *(numeric-only)* | CLIENT_ERROR |
| 420 | *(numeric-only)* | CLIENT_ERROR |
| 421 | `421 Misdirected Request` | CLIENT_ERROR |
| 422 | *(numeric-only)* | CLIENT_ERROR |
| 423 | *(numeric-only)* | CLIENT_ERROR |
| 424 | *(numeric-only)* | CLIENT_ERROR |
| 425 | *(numeric-only)* | CLIENT_ERROR |
| 426 | *(numeric-only)* | CLIENT_ERROR |
| 427 | *(numeric-only)* | CLIENT_ERROR |
| 428 | *(numeric-only)* | CLIENT_ERROR |
| 429 | `429 Too Many Requests` | CLIENT_ERROR |

### 5xx — Server error

Every 5xx code carries `SERVER_ERROR`.

| Code | Reason | Flags |
|---|---|---|
| 500 | `500 Internal Server Error` | SERVER_ERROR |
| 501 | `501 Not Implemented` | SERVER_ERROR + CACHEABLE |
| 502 | `502 Bad Gateway` | SERVER_ERROR |
| 503 | `503 Service Temporarily Unavailable` | SERVER_ERROR |
| 504 | `504 Gateway Time-out` | SERVER_ERROR |
| 505 | `505 HTTP Version Not Supported` | SERVER_ERROR |
| 506 | *(numeric-only)* | SERVER_ERROR |
| 507 | `507 Insufficient Storage` | SERVER_ERROR |

### nginx extension codes

All extension codes are stored numeric-only. Their security-relevant behaviors
are implemented by their callers and the special-response module, not by the
registry.

| Code | Constant | Flags | Behavior |
|---|---|---|---|
| 444 | `NGX_HTTP_CLOSE` | — | Closes the connection without sending a response. |
| 494 | `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | CLIENT_ERROR | Rewritten to `400` on the wire. |
| 495 | `NGX_HTTPS_CERT_ERROR` | CLIENT_ERROR | Rewritten to `400` on the wire. |
| 496 | `NGX_HTTPS_NO_CERT` | CLIENT_ERROR | Rewritten to `400` on the wire. |
| 497 | `NGX_HTTP_TO_HTTPS` | CLIENT_ERROR | Rewritten to `400` on the wire. |
| 498 | *(canceled — invalid host name)* | CLIENT_ERROR | Mapped to the 404 error page. |
| 499 | `NGX_HTTP_CLIENT_CLOSED_REQUEST` | CLIENT_ERROR | Logged when the client closed the connection. |


## Cacheable-by-default codes

Exactly **12** codes carry the `NGX_HTTP_STATUS_CACHEABLE` flag:

```text
200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501
```

This is **metadata only**. The flag exposes the property that a response with
these codes is heuristically cacheable by default — consistent with the
heuristic-freshness reasoning described in RFC 9111 — but it **introduces no new
caching behavior**. nginx's existing cache layer is unchanged; the flag is
informational and is read only through `ngx_http_status_is_cacheable()`.

## Build mode: `--with-http_status_validation`

Status validation is an opt-in build mode. It is **off by default**.

- **Disabled (default).** `ngx_http_status_set()` is the zero-overhead
  `static ngx_inline` setter described in the [API](#api) section. It is
  identical to the legacy direct field write, so there is **zero added
  overhead** and wire output is byte-identical to previous releases.
- **Enabled (`--with-http_status_validation`).** The build defines
  `NGX_HTTP_STATUS_VALIDATION 1`, and `ngx_http_status_set()` becomes a real
  validating function: an out-of-range code (outside `100..599`) returns
  `NGX_ERROR`, and the caller logs an `NGX_LOG_ERR` and falls back to
  `NGX_HTTP_INTERNAL_SERVER_ERROR`.

In **either** mode, **proxied/upstream responses bypass strict validation** so
that an upstream-supplied status is passed through unchanged. This is because
`ngx_http_status_set()` short-circuits when `r->upstream` is set.

Configure usage:

```bash
# default (validation disabled, zero overhead)
auto/configure && make

# enable RFC 9110 Section 15 validation
auto/configure --with-http_status_validation && make
```

