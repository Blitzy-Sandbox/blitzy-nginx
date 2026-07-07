# HTTP Status Code API Reference

The centralized HTTP status code registry is the single authority for HTTP response status metadata in nginx. It is declared in `src/http/ngx_http_status.h` and implemented in `src/http/ngx_http_status.c`, and it is backed by a static, read-only registry seeded from the status classes defined in [RFC 9110 "HTTP Semantics", Section 15](https://datatracker.ietf.org/doc/html/rfc9110). For each status code the registry records its numeric value, wire reason phrase, class and behavior flags, and a back-reference to the defining RFC section.

Before this module, status handling was spread across three independent mechanisms: direct assignment to the `r->headers_out.status` field, return-based propagation of `NGX_HTTP_*` codes through request finalization, and a private reason-phrase table inside the HTTP/1.x header filter. This API consolidates all three into one registry-backed surface — `ngx_http_status_set()` is the single write path, `ngx_http_status_reason()` is the single source of the reason phrase, and `ngx_http_status_validate()` and `ngx_http_status_is_cacheable()` expose the registry's metadata. The numeric `NGX_HTTP_*` constants and direct field assignment remain fully supported (see the Backward compatibility section, below).

## The `ngx_http_status_def_t` type

Each registry entry is described by a single record type, defined in `src/http/ngx_http_status.h`:

```c
typedef struct {
    ngx_uint_t    code;          /* numeric HTTP status code (e.g., 200, 404) */
    ngx_str_t     reason;        /* reason phrase, e.g. "200 OK" (ngx_str_t) */
    ngx_uint_t    flags;         /* class/behavior flags (bit field) */
    const char   *rfc_section;   /* RFC 9110 §15 back-reference, e.g. "RFC9110#15.5.5" */
} ngx_http_status_def_t;
```

- `code` — the numeric HTTP status code the entry describes (for example `200` or `404`).
- `reason` — the reason phrase, stored as an `ngx_str_t`. The registry holds nginx's historical reason text (for example `OK` for `200`, `Not Found` for `404`, `Not Allowed` for `405`); the HTTP/1.x header filter prepends the numeric code and a space to render the full wire status line (`200 OK`, `404 Not Found`, `405 Not Allowed`). Codes that nginx ships without a phrase carry an empty `ngx_str_t` (`ngx_null_string`).
- `flags` — a bitwise OR of the `NGX_HTTP_STATUS_*` flags below, encoding the code's status class and cacheability.
- `rfc_section` — a human-readable RFC back-reference string, such as `"RFC9110#15.3.1"`. A few codes reference other RFCs: `429` maps to `"RFC6585#4"` and `507` maps to `"RFC4918#11.5"`.

## Flag macros `NGX_HTTP_STATUS_*`

The `flags` member is a bit field built from the following macros, defined in `src/http/ngx_http_status.h`. Each is a distinct power of two, so several may be combined for a single code with a bitwise OR.

| Macro | Value | Meaning |
|-------|-------|---------|
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | Response is heuristically cacheable per RFC 9110 §15.1 |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0002` | 1xx Informational class |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0004` | 4xx Client Error class |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0008` | 5xx Server Error class |

The three class flags map directly to the RFC 9110 §15 status classes — `NGX_HTTP_STATUS_INFORMATIONAL` for 1xx, `NGX_HTTP_STATUS_CLIENT_ERROR` for 4xx, and `NGX_HTTP_STATUS_SERVER_ERROR` for 5xx — while `NGX_HTTP_STATUS_CACHEABLE` marks the heuristically cacheable codes of RFC 9110 §15.1. Because the flags are independent bits, a single registry entry combines them. For example, `404` is seeded with both its class flag and its cacheability flag:

```c
NGX_HTTP_STATUS_CLIENT_ERROR | NGX_HTTP_STATUS_CACHEABLE
```

## Public API functions

The module exposes five functions. Each implementation is capped at 50 lines, excluding comments.

### `ngx_http_status_set`

```c
ngx_int_t  ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
```

The single sanctioned write seam for a response status — the mediated replacement for `r->headers_out.status = CODE;`. When compiled with validation enabled it validates the code first, then writes `r->headers_out.status`.

- Parameters: `r` — the current request; `status` — the numeric status code to set.
- Returns: `NGX_OK` on success. Returns `NGX_ERROR` only in a validation (strict) build, and only when a non-upstream code is out of range.
- Upstream pass-through: when `r->upstream != NULL`, the backend's status is written through unconditionally and is never rejected or transformed — validation on this path is log-only. This preserves byte-for-byte pass-through of non-standard backend codes.
- Default build: with validation disabled (the default), the function always writes the field and returns `NGX_OK`, so its behavior is byte-identical to the old direct assignment.

### `ngx_http_status_validate`

```c
ngx_int_t  ngx_http_status_validate(ngx_uint_t status);
```

An RFC 9110 range and class check. Strict versus standard behavior is selected at compile time via `#ifdef NGX_HTTP_STATUS_VALIDATION`, so the strict branch is fully compiled out — carrying zero runtime cost — in the default build. It is an inline candidate.

- Parameters: `status` — the numeric status code to check.
- Returns: `NGX_OK` when the code is acceptable; `NGX_ERROR` when it is rejected.
- Strict mode (built with `--with-http_status_validation`, i.e. `#if (NGX_HTTP_STATUS_VALIDATION)`): rejects codes outside the range 100–599 with `NGX_ERROR` and logs RFC violations.
- Standard mode (default): permissive — always returns `NGX_OK`; suspicious codes are logged at debug level only.

### `ngx_http_status_reason`

```c
ngx_str_t  ngx_http_status_reason(ngx_uint_t status);
```

The single source of the reason phrase. The lookup is an O(1) direct array index — there is no hashing and no search.

- Parameters: `status` — the numeric status code to look up.
- Returns: the registry entry's `reason` as an `ngx_str_t`. For gap codes with no shipped phrase (for example `203`, `205`, `305`, `306`, `407`, `417`, `506`) and for out-of-range codes, it returns an empty `ngx_str_t` (`{ 0, NULL }`, i.e. `.len == 0`), signalling the caller to render the numeric status instead.
- The returned phrase uses nginx's historical spelling (for example `OK`, `Not Found`, `Not Allowed`). The HTTP/1.x header filter prepends the numeric code and a space to produce the full wire status line, such as `200 OK`, `404 Not Found`, `301 Moved Permanently`, and `500 Internal Server Error`.
- This function is delegated to by the HTTP/1.x header filter, the error-page funnel (`ngx_http_special_response.c`), and the HTTP/2 and HTTP/3 serializers.

### `ngx_http_status_register`

```c
ngx_int_t  ngx_http_status_register(void);
```

Configuration-phase registry seeding and finalization. It is invoked once, before the first worker fork.

- Parameters: none.
- Returns: `NGX_OK`.
- There is no runtime mutation API: the registry is read-only after initialization and cannot be modified at runtime or per request.

### `ngx_http_status_is_cacheable`

```c
ngx_uint_t  ngx_http_status_is_cacheable(ngx_uint_t status);
```

A boolean flag test for heuristic cacheability.

- Parameters: `status` — the numeric status code to test.
- Returns: a non-zero value if the code's registry entry has `NGX_HTTP_STATUS_CACHEABLE` set; `0` for non-cacheable or unknown codes.
- The cacheable set seeded by the registry is {200, 203, 204, 206, 301, 308, 404, 405, 410, 414, 501} — the RFC 9110 §15.1 heuristically cacheable codes present in the shipped table.

## Registry properties

- The registry is a static, read-only-after-init array.
- It is indexed in O(1) by direct array offset — no hashing and no search.
- It consumes under 1 KB per worker.
- It is shared lock-free across worker processes: no thread-local storage and no locks.
- It is populated during the configuration phase, before the first worker fork.
- Each API function implementation is capped at 50 lines, excluding comments.

Design invariants: there is no status caching beyond direct array indexing, no status aliasing, and no runtime registry-mutation API.

## Usage examples

Convert a direct status assignment to the mediated write seam.

Old pattern:

```c
r->headers_out.status = NGX_HTTP_NOT_FOUND;
```

New pattern:

```c
if (ngx_http_status_set(r, 404) != NGX_OK) {
    /* log error */
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

Look up a reason phrase and fall back to the numeric status when the registry has no phrase for the code:

```c
ngx_str_t  reason;

reason = ngx_http_status_reason(r->headers_out.status);

if (reason.len == 0) {
    /* no shipped phrase: render the numeric status only */
}
```

Test whether a status code is heuristically cacheable:

```c
if (ngx_http_status_is_cacheable(r->headers_out.status)) {
    /* eligible for heuristic caching */
}
```

## Backward compatibility

All 45 existing `NGX_HTTP_*` numeric constants remain defined in `src/http/ngx_http_request.h` — for example `NGX_HTTP_OK` (200) and `NGX_HTTP_NOT_FOUND` (404) — so existing code that references them continues to compile unchanged. Direct assignment to `r->headers_out.status` continues to work and has not been removed; `ngx_http_status_set()` is the sanctioned write seam, not a mandatory one.

The validation layer is opt-in. It is enabled with the `--with-http_status_validation` configure switch, which is off by default and emits the `NGX_HTTP_STATUS_VALIDATION` compile-time define. With the switch off, the strict validation branch is compiled out entirely, so the default binary is byte-identical to the pre-refactor build.
