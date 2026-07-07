# HTTP Status Code API Reference

The centralized HTTP status code registry is the single authority for HTTP response status metadata in nginx. It is declared in `src/http/ngx_http_status.h` and implemented in `src/http/ngx_http_status.c`, and it is backed by a static, read-only registry seeded from the status classes defined in [RFC 9110 "HTTP Semantics", Section 15](https://datatracker.ietf.org/doc/html/rfc9110). For each status code the registry records its numeric value, wire reason phrase, class and behavior flags, and a back-reference to the defining RFC section.

Before this module, status handling was spread across three independent mechanisms: direct assignment to the `r->headers_out.status` field, return-based propagation of `NGX_HTTP_*` codes through request finalization, and a private reason-phrase table inside the HTTP/1.x header filter. This API consolidates all three into one registry-backed surface — `ngx_http_status_set()` is the single write path, `ngx_http_status_reason()` is the single source of the reason phrase, and `ngx_http_status_validate()` and `ngx_http_status_is_cacheable()` expose the registry's metadata. The numeric `NGX_HTTP_*` constants and direct field assignment remain fully supported (see the Backward compatibility section, below).

## The `ngx_http_status_def_t` type

Each registry entry is described by a single record type, defined in `src/http/ngx_http_status.h`:

```c
typedef struct {
    const char   *line;          /* full wire status line, e.g. "200 OK" */
    uint16_t      line_len;      /* length of line, in bytes */
    uint16_t      code;          /* numeric HTTP status code (e.g., 200, 404) */
    uint16_t      flags;         /* class/behavior flags (bit field) */
    uint16_t      rfc_section;   /* packed RFC 9110 §15 back-reference (see below) */
} ngx_http_status_def_t;
```

The record is deliberately compact: four `uint16_t` fields pack alongside the leading pointer so each entry is 16 bytes on common LP64 targets, keeping the whole registry table well under the 1 KB-per-worker budget the design mandates.

- `line` and `line_len` — the full precomputed **wire status line**, stored as a `const char *` / length pair that forms an `ngx_str_t`-style value: the numeric code, a single space, and the reason phrase, for example `"200 OK"` for `200`, `"404 Not Found"` for `404`, `"405 Not Allowed"` for `405`. The public accessor `ngx_http_status_line()` returns this pair as a genuine `ngx_str_t`, so the HTTP/1.x header filter emits the status line with a single copy and no per-response formatting. The bare reason phrase (`"OK"`, `"Not Found"`) is derived on demand by `ngx_http_status_reason()`, which skips the invariant four-character `"NNN "` code prefix. Codes that nginx ships without a phrase carry a `NULL`/`0` pair (`line == NULL`, `line_len == 0`).
- `code` — the numeric HTTP status code the entry describes (for example `200` or `404`).
- `flags` — a bitwise OR of the `NGX_HTTP_STATUS_*` flags below, encoding the code's status class and cacheability.
- `rfc_section` — a packed back-reference to the RFC 9110 §15 subsection that defines the code, encoded as `(subsection << 8) | item`. For example `200` (§15.3.1) is `0x0301`, `404` (§15.5.5) is `0x0505`, and `500` (§15.6.1) is `0x0601`. Codes not defined in RFC 9110 §15 — such as `429` (RFC 6585), `507` (RFC 4918), and the unseeded gap rows — use `NGX_HTTP_STATUS_RFC_NONE` (`0`), meaning "no RFC 9110 §15 reference". Validation logging decodes the value back into `15.<subsection>.<item>` form (see `ngx_http_status_validate`, below).

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
- Strict mode (built with `--with-http_status_validation`, i.e. `#if (NGX_HTTP_STATUS_VALIDATION)`): rejects codes outside the range 100–599 with `NGX_ERROR`. In this build `ngx_http_status_set()` additionally logs rejected codes and, via `ngx_http_status_check()`, logs RFC 9110 violations — decoding each offending code's packed `rfc_section` into `15.<subsection>.<item>` form.
- Standard mode (default): purely permissive — always returns `NGX_OK` and performs no rejection. Diagnostic logging in this mode is done by `ngx_http_status_set()`, not by `validate()`: in a debug build (`NGX_DEBUG`) it logs locally generated out-of-range codes at `NGX_LOG_DEBUG_HTTP`. In a non-debug default build that diagnostic compiles to nothing, so the write path carries no extra cost and wire output is byte-identical to historical nginx.

### `ngx_http_status_line`

```c
ngx_str_t  ngx_http_status_line(ngx_uint_t status);
```

The single source of the full **wire status line** — the numeric code, a single space, and the reason phrase (`200 OK`, `404 Not Found`, `301 Moved Permanently`, `500 Internal Server Error`). The lookup is an O(1) direct array index — there is no hashing and no search.

- Parameters: `status` — the numeric status code to look up.
- Returns: the registry entry's precomputed `line` as an `ngx_str_t`. For gap codes with no shipped phrase (for example `203`, `205`, `305`, `306`, `407`, `417`, `506`) and for out-of-range codes, it returns an empty `ngx_str_t` (`{ 0, NULL }`, i.e. `.len == 0`), signalling the caller to render the numeric status instead.
- This is the accessor the HTTP/1.x header filter uses on the hot path: because the line is precomputed and interned in the registry, the filter emits it with a single copy and performs **no** per-response formatting. When it returns an empty value the filter falls back to the `"%03ui "` numeric writer. The HTTP/2 and HTTP/3 serializers do **not** call it — their `:status` pseudo-header is numeric-only (HPACK/QPACK carry no reason phrase).

### `ngx_http_status_reason`

```c
ngx_str_t  ngx_http_status_reason(ngx_uint_t status);
```

The single source of the **bare** reason phrase — the reason text with no numeric code prefix. The lookup is an O(1) direct array index — there is no hashing and no search.

- Parameters: `status` — the numeric status code to look up.
- Returns: the bare reason phrase as an `ngx_str_t`, derived from the registry's combined `line` by skipping the invariant four-character `"NNN "` prefix (so `200` yields `OK`, `404` yields `Not Found`). For gap codes with no shipped phrase (for example `203`, `205`, `305`, `306`, `407`, `417`, `506`) and for out-of-range codes, it returns an empty `ngx_str_t` (`{ 0, NULL }`, i.e. `.len == 0`), signalling the caller to render the numeric status instead.
- The returned phrase uses nginx's historical spelling (for example `OK`, `Not Found`, `Not Allowed`); the numeric code and space that complete the wire line are already carried by `ngx_http_status_line()` (above).
- This function is consumed by the error-page funnel (`ngx_http_special_response.c`) for a debug-level diagnostic. The HTTP/1.x header filter renders the wire status line via `ngx_http_status_line()`, **not** via this function. The HTTP/2 and HTTP/3 serializers do **not** call it either: their `:status` pseudo-header is numeric-only (HPACK/QPACK carry no reason phrase), so those code paths read the numeric `r->headers_out.status` field directly and never emit a reason string.

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
