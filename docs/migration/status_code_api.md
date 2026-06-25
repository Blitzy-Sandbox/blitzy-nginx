# Status-Code API Migration Guide

nginx now centralizes every HTTP response-status write behind a small **facade
API** backed by an immutable **registry** — a `static const ngx_http_status_def_t[]`
table hosted in `src/http/ngx_http_request.c` and declared in the umbrella header
`src/http/ngx_http.h`. This guide explains how to migrate direct
`r->headers_out.status = ...` assignments to `ngx_http_status_set(r, code)`
**without changing any wire-observable behavior**.

Migration is **opt-in and non-breaking**: the new API is layered *additively* over
the existing field. Direct assignment keeps working, every `NGX_HTTP_*` constant is
retained, and strict validation is enabled only when you build with
`--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`. You can adopt the facade
incrementally — one call site at a time — or not at all.

> **Related documents**
>
> - [Traceability Matrix](traceability_matrix.md) — the bidirectional mapping of
>   every `NGX_HTTP_*` constant and every converted call site to its target API
>   call.
> - [Status-Code API Reference](../api/status_codes.md) — the full signatures,
>   parameters, return values, error semantics, and RFC 9110 §15 notes for the
>   five facade functions. This guide links to that reference rather than
>   reproducing it.

## The facade API at a glance

The facade consists of exactly **five functions**, declared in `ngx_http.h` and
implemented in `ngx_http_request.c`:

| Function | Signature | Purpose |
|---|---|---|
| `ngx_http_status_set` | `ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t code)` | Write the response status through the registry facade — the canonical replacement for `r->headers_out.status = code;`. Returns `NGX_OK` on success; a non-`NGX_OK` value (treat as `NGX_ERROR`) only when strict validation rejects `code`. |
| `ngx_http_status_validate` | `ngx_int_t ngx_http_status_validate(ngx_uint_t code)` | Report whether `code` is acceptable (range check plus RFC 9110 §15 conformance under strict mode). |
| `ngx_http_status_reason` | `ngx_str_t *ngx_http_status_reason(ngx_uint_t code)` | Resolve the default reason phrase for `code`, or `NULL` when no phrase is registered. |
| `ngx_http_status_register` | `ngx_int_t ngx_http_status_register(const ngx_http_status_def_t *def)` | Compile/init-time entry point for adding a status definition to the registry. |
| `ngx_http_status_is_cacheable` | `ngx_uint_t ngx_http_status_is_cacheable(ngx_uint_t code)` | Report whether `code` is heuristically cacheable per RFC 9111. |

This guide focuses on `ngx_http_status_set()`, the one function that replaces a
direct field write. For the complete reference of all five functions, see the
[Status-Code API Reference](../api/status_codes.md).

> **Note — two similarly named types.** Do not confuse the new registry record
> type `ngx_http_status_def_t` with the pre-existing, unrelated `ngx_http_status_t`
> struct (fields `http_version`, `code`, `count`, `start`, `end`) in `ngx_http.h`.
> The latter parses **upstream** status lines and is untouched by this refactor;
> the two types were deliberately given distinct names to avoid a collision.

## Pattern A — direct field assignment → facade setter

This is the heart of the migration. A **Pattern A** site is a content handler or
status-setting filter that writes the status field directly, for example the
static module's `ngx_http_static_handler` (which returns `ngx_int_t`). The
conversion replaces the bare assignment with a guarded call to
`ngx_http_status_set()`.

### Form 1 — `NGX_HTTP_OK` (the static-module exemplar)

```c
// BEFORE
r->headers_out.status = NGX_HTTP_OK;

// AFTER
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", (ngx_uint_t) NGX_HTTP_OK);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

### Form 2 — numeric literal `404`

```c
// OLD
r->headers_out.status = NGX_HTTP_NOT_FOUND;

// NEW
if (ngx_http_status_set(r, 404) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "invalid status code: 404");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

Both forms are equivalent — you may pass either an `NGX_HTTP_*` constant or a bare
numeric literal, since `ngx_http_status_set()` takes an `ngx_uint_t code`.

### What changes, and what stays the same

`ngx_http_status_set()` writes `r->headers_out.status` **exactly as before**. In the
**default build** (validation disabled), the setter compiles to essentially the same
single store as the legacy direct assignment, so there is no measurable overhead —
it stays well within the project's <2% latency budget.

Only the status write itself is wrapped. Any adjacent lines that previously
accompanied the assignment are **preserved unchanged**. For instance, in the static
module the surrounding `r->headers_out.content_length_n` and
`r->headers_out.last_modified_time` writes are left exactly as they were:

```c
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", (ngx_uint_t) NGX_HTTP_OK);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}

r->headers_out.content_length_n = of.size;     /* unchanged */
r->headers_out.last_modified_time = of.mtime;   /* unchanged */
```

Likewise, where a handler also reset the reason phrase
(`r->headers_out.status_line.len = 0;`), that line is retained as-is — the facade
does not touch `status_line`.

The `return NGX_HTTP_INTERNAL_SERVER_ERROR;` failure branch shown above is the form
used by functions that **return `ngx_int_t`** — the common case, such as
`ngx_http_static_handler`. Functions with a different return contract need a
different failure branch; the next section covers those variants.

## Special-contract variants

The guarded call to `ngx_http_status_set()` is uniform, but its **failure branch
must match the enclosing function's return contract**. There are three contracts in
the HTTP source tree.

### (a) `void` handler — finalize directly

Some handlers return `void` and drive the request to completion themselves via
`ngx_http_finalize_request()`. The DAV module's `ngx_http_dav_put_handler` is the
canonical example: it returns `void`, all of its failure paths call
`ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR); return;` (never
`return <value>;`), and it sets the status from a `status` **variable** that holds
`NGX_HTTP_CREATED` (201) for a freshly created resource or `NGX_HTTP_NO_CONTENT`
(204) for an overwrite.

```c
// BEFORE (ngx_http_dav_put_handler — returns void; `status` is 201 or 204)
r->headers_out.status = status;

// AFTER
if (ngx_http_status_set(r, status) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", status);
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return;
}
```

### (b) pointer-returning function — return `NULL`

A function that returns a pointer signals failure by returning `NULL`. The image
filter's `ngx_http_image_json` returns `ngx_buf_t *`, so its failure branch is
`return NULL;`.

```c
// BEFORE (ngx_http_image_json — returns ngx_buf_t *)
r->headers_out.status = NGX_HTTP_OK;

// AFTER
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", (ngx_uint_t) NGX_HTTP_OK);
    return NULL;
}
```

### Failure branch by return contract

| Enclosing function returns | Failure branch |
|---|---|
| `ngx_int_t` (most handlers/filters) | `return NGX_HTTP_INTERNAL_SERVER_ERROR;` |
| `void` (e.g. dav PUT handler) | `ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR); return;` |
| pointer (e.g. `ngx_buf_t *`) | `return NULL;` |

## Patterns B and C — adopted centrally, not per-`return`

Not every module writes the status field directly. Two other patterns produce a
status through different mechanisms, and **neither is rewritten at the individual
statement**.

### Pattern B — return-code convention

Many handlers signal their status by `return NGX_HTTP_*;` instead of assigning the
field — for example the access module's `403`, `auth_basic`'s `401`/`403`, and the
index module's `403`/`404`. These are **not** converted at each `return` statement.
The returned code propagates through `ngx_http_finalize_request` into the
special-response handler, which sets `r->err_status`; the wire status then
materializes **centrally**.

### Pattern C — config/script-driven

The rewrite module's `return` directive parses a numeric status into a script
opcode whose `e->status` flows back into the core. This too is adopted centrally,
not per-statement.

### Where the API is actually adopted

The key architectural point is that **API adoption for error and return flows
happens at the central finalization choke points in `ngx_http_core_module.c`** —
specifically `ngx_http_send_response()` (which receives the status as a parameter)
and `ngx_http_send_header()` (which routes `r->err_status` through the facade). As a
result, every Pattern B and Pattern C module "adopts the API centrally" with **no
per-`return` edits** anywhere in those modules. For the exact choke points and the
constants they cover, see Table C of the
[Traceability Matrix](traceability_matrix.md).

#### Late choke point — `ngx_http_send_header()` coerces rather than returns

The failure-branch table above lists `return NGX_HTTP_INTERNAL_SERVER_ERROR;` as the
form for an `ngx_int_t` function, and that is exactly what the **early** choke point
`ngx_http_send_response()` uses: it runs *before* the response is committed, so a
returned 500 is finalized into a clean error response. `ngx_http_send_header()` is
the **late** choke point and is the one documented exception to that rule. By the
time it runs, the special-response flow has already committed to `r->err_status` and
laid out the (empty) error body, so a bare `return` there would abort header
emission and close the connection with **zero bytes** — an empty close the client
observes as no response at all (HTTP&nbsp;000), not a status line. It therefore
**coerces** a validation-rejected `err_status` to `NGX_HTTP_INTERNAL_SERVER_ERROR`
and falls through to the header filter, deterministically materializing the same
clean `500` this guide documents as the failure response. This coercion is reached
**only** in the opt-in validation build for a locally-generated, unmodelled code
(for example a bare `return 451;`); in the default build `ngx_http_status_set()`
never fails, so the branch is unreachable and the wire output is byte-identical to
stock nginx. The detection-by-logging behavior is unchanged either way —
`invalid status code: N` is still logged for every rejected code.

The `error_page` parsing and dispatch logic — `overwrite` handling, complex-value
URI evaluation, internal redirect, and named-location dispatch — is **preserved
exactly**. The registry integrates only at the default reason-phrase lookup; it does
not alter how `error_page` is parsed or dispatched.

## No new `#include` is required

Adopting the facade requires **zero import changes** in any module — in-tree or
third-party. The five facade functions are declared in the umbrella header
`ngx_http.h`, which in turn includes `ngx_http_request.h` (carrying the
`ngx_http_status_def_t` typedef and the `NGX_HTTP_STATUS_*` classification-flag
macros). Because every HTTP module already does `#include <ngx_http.h>`, the
declarations arrive automatically.

There is therefore **no import-rewrite sweep** across the module tree: a converted
call site such as `ngx_http_static_module.c` keeps its existing three includes
(`ngx_config.h`, `ngx_core.h`, `ngx_http.h`) unchanged.

## Migrating a third-party module

Third-party module authors can migrate at their own pace, or not at all. The steps
are:

1. **No forced migration.** Keep your existing `r->headers_out.status = code;`
   exactly as it is — it remains fully functional. Doing nothing is a valid choice.
2. **Optional adoption.** Replace a direct assignment with
   `ngx_http_status_set(r, code)` and check its result against `!= NGX_OK`.
3. **Adapt the failure branch to your function's return contract.** Use
   `return NGX_HTTP_INTERNAL_SERVER_ERROR;` for an `ngx_int_t` function,
   `ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR); return;` for a
   `void` handler, and `return NULL;` for a pointer-returning function — see the
   [failure branch by return contract](#failure-branch-by-return-contract) table
   above.
4. **No `#include` change required.** The declaration arrives via the
   already-included `ngx_http.h`; you do not add any new header.

## Backward-compatibility guarantees

The refactor is strictly additive. The following guarantees hold:

- **All existing `NGX_HTTP_*` numeric constants are retained** and are never
  removed.
- **Direct `r->headers_out.status = ...` assignment remains functional.** The API
  is additive and dual-path; the legacy write and the facade coexist.
- **Validation is strictly opt-in** via
  `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` (compile-time macro
  `NGX_HTTP_STATUS_VALIDATION`). The default build is permissive with effectively
  zero overhead.
- **The `NGX_MODULE_V1` module ABI is unchanged**, so third-party modules continue
  to build and load without recompilation against a changed interface.
- **`nginx.conf` behavior is unchanged** — no runtime directive semantics change.
- **Graceful binary upgrade is preserved** — a new binary runs against an existing
  configuration.

## Upstream pass-through

`ngx_http_status_set()` checks for `r->upstream` **before** applying any strict
validation. When the status originates from a proxied upstream, the setter writes it
to `r->headers_out.status` verbatim and returns `NGX_OK` without consulting the
registry. Consequently, an origin-assigned status that flows through the proxy,
FastCGI, SCGI, uwsgi, gRPC, or memcached modules is **never transformed and never
rejected** — the proxied value is emitted exactly as the origin sent it.

The validator also **whitelists nginx's internal sentinel codes** — `444`
(`NGX_HTTP_CLOSE`) and `494`–`499`. These are internal, non-wire-emitted codes that
fall numerically within the `100`–`599` range; `ngx_http_status_validate()`
short-circuits them to acceptance so they are never rejected, even in a validation
build (`--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`).

## Deprecation roadmap

This roadmap is **guidance, not a dated schedule.** It uses stage labels rather than
calendar dates or time estimates, consistent with the one-phase,
logically-sequenced nature of the refactor (AAP §0.4.4 / §0.7.2). No stage carries a
deadline, and direct assignment never stops working.

- **Stage 0 — Available now.** The facade API ships; direct field assignment is
  fully supported.
- **Stage 1 — Recommended.** New code uses `ngx_http_status_set()`; existing code
  may remain on direct assignment.
- **Stage 2 — Encouraged.** Opt into validation by building with
  `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` in test or staging environments to
  surface non-conforming status codes early.
- **Long-term.** Direct field assignment is supported **indefinitely** for
  compatibility. The facade is encouraged for forward compatibility and optional
  validation. **No removal of direct assignment is planned.**
