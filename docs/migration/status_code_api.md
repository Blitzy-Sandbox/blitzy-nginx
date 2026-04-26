# Third-Party Module Migration Guide — Centralized HTTP Status Code API

**Project:** NGINX 1.29.5 Centralized HTTP Status Code Refactor
**Audience:** Authors of third-party NGINX modules loaded via `load_module`, distribution maintainers carrying NGINX patches, and companies maintaining custom NGINX builds
**Companion documents:** [API reference](../api/status_codes.md), [decision log](../architecture/decision_log.md), [architecture diagrams](../architecture/status_code_refactor.md), [observability integration](../architecture/observability.md)

## Overview

NGINX 1.29.5 introduces a **centralized HTTP status code API** consisting of five new public functions reachable through `<ngx_http.h>`:

- `ngx_http_status_set(r, status)` — set the response status code with optional validation
- `ngx_http_status_validate(status)` — check whether a code is in the RFC 9110 range (100–599)
- `ngx_http_status_reason(status)` — look up the canonical RFC 9110 reason phrase for a code
- `ngx_http_status_is_cacheable(status)` — query the cacheability flag for a status code
- `ngx_http_status_register(def)` — (init-time only) extend the registry with a custom code

This guide describes how to migrate a third-party NGINX module from the legacy direct-assignment pattern (`r->headers_out.status = NGX_HTTP_NOT_FOUND;`) to the new API (`ngx_http_status_set(r, NGX_HTTP_NOT_FOUND)`). The API offers four benefits over direct assignment:

1. **Range validation** — out-of-range codes (`<100` or `>599`) are detected at the assignment site rather than appearing as malformed status lines on the wire.
2. **Strict-mode RFC 9110 conformance** — opt-in via the `--with-http_status_validation` configure flag for builders who want stricter checking baked into their distribution.
3. **Structured debug tracing** — every assignment emits a single `NGX_LOG_DEBUG_HTTP` line correlated by the `$request_id` access-log variable, enabling end-to-end status-code observability.
4. **Future extensibility** — reason-phrase lookup, cacheability metadata, and class flags are available without re-implementing them per module.

### Migration is OPTIONAL

**Adoption of the new API is NEVER required.** Per the project's permanent backward-compatibility mandate ([decision D-003](../architecture/decision_log.md#architectural-decisions) and the AAP's R-6 rule):

- All `NGX_HTTP_*` preprocessor macros remain defined at their current numeric values forever.
- Direct assignment to `r->headers_out.status` continues to compile and produce identical wire output.
- The `NGX_MODULE_V1` module-ABI signature is unchanged; binary modules built against pristine 1.29.5 headers continue to load.
- Filter chain interfaces (`ngx_http_output_header_filter_pt`, `ngx_http_output_body_filter_pt`) are byte-for-byte preserved.

You can migrate at your own pace, file by file, or never. NGINX core has migrated all in-tree direct-assignment call sites in a single commit; third-party modules can take any timeline that suits their release cadence.

### When to Migrate

| Scenario | Recommendation |
|---|---|
| New module under active development | **Migrate** — adopt the new API for greenfield code; you get validation and observability for free. |
| Stable module with sporadic maintenance | **Defer** — direct assignment continues to work; migrate when you next touch the file for an unrelated reason. |
| Module distributed pre-built across many NGINX versions | **Conditional** — use a `#ifdef` guard on a sentinel macro (see [Step 1](#step-1-verify-nginx-version-compatibility)) so the module compiles against both pre-refactor and post-refactor NGINX. |
| Internal/private module on a single fixed NGINX version | **Migrate freely** — no compatibility constraints. |
| Module that emits codes outside `100..599` (e.g., experimental codes) | **Defer with caution** — strict mode will reject these; the permissive default still accepts them, but a future NGINX release may tighten this. Migrate AND ensure your build does not pass `--with-http_status_validation`. |
| Module that simply *reads* the status (e.g., `if (r->headers_out.status == NGX_HTTP_OK)`) | **No migration needed** — read paths are unchanged; see [Pattern 4](#pattern-4-read-only-comparison--unchanged). |

## Backward Compatibility Guarantees

The refactor is **strictly additive**. The following invariants are contractual and will be preserved in all NGINX 1.29.x patch releases and forward-compatible into 1.30.x and beyond unless explicitly versioned out via a major release announcement.

| Invariant | Anchor | Implication for Third-Party Modules |
|---|---|---|
| `#define NGX_HTTP_*` macros remain defined at their current numeric values | AAP R-1; `src/http/ngx_http_request.h` lines 74–145 | `if (rc == NGX_HTTP_OK)` and `r->headers_out.status = NGX_HTTP_NOT_FOUND;` continue to work identically. No source change required. |
| Direct field assignment (`r->headers_out.status = X;`) continues to compile | AAP R-6 | Existing modules that have not migrated still compile. The field is not deprecated, hidden, or renamed. |
| `NGX_MODULE_V1` ABI unchanged | AAP R-2 | Pre-compiled `.so` modules built against pristine NGINX 1.29.5 headers continue to load against the refactored binary. No recompilation required to keep modules functional. |
| `ngx_http_output_header_filter_pt`, `ngx_http_output_body_filter_pt` signatures unchanged | AAP R-4 | Filter modules retain their function signatures. No filter-chain reordering or registration-call-shape changes. |
| `nginx.conf` directive behavior unchanged for `error_page`, `return`, `proxy_intercept_errors` | AAP § 0.8.5 preservation mandate | Configuration files that reference these directives need NO changes. The `error_page` parser (function `ngx_http_core_error_page`) at `ngx_http_core_module.c:4914-5029` is preserved byte-for-byte. |
| Wire format unchanged: `ngx_http_status_lines[]` table preserved byte-for-byte | AAP D-008 | HTTP/1.1 reason phrases on the wire are unchanged. Browsers and HTTP clients see exactly the same status lines as before. |
| `$status`, `$upstream_status` access-log variables preserved | AAP § 0.3.2 | Log-analysis tools and dashboards continue to work without changes. The `$status` variable still emits the post-masquerade wire status (e.g., 400 for an internal 497). |
| HTTP/2 and HTTP/3 numeric `:status` pseudo-header unchanged | AAP § 0.3.2 | HPACK-encoded HTTP/2 responses and QPACK-encoded HTTP/3 responses are byte-for-byte identical to the pre-refactor binary on identical inputs. |

**What this guarantees in practice:**

- A module compiled against NGINX 1.29.4 will load and run on NGINX 1.29.5 (the refactored release) with no changes.
- A module that uses only `#define NGX_HTTP_*` macros for read-only comparisons (`if (rc == NGX_HTTP_FORBIDDEN)`) requires zero migration work.
- A module that uses direct field assignment (`r->headers_out.status = NGX_HTTP_OK;`) requires zero migration work to keep functioning; migration is a quality-of-life upgrade, not a survival requirement.

**What is explicitly NOT promised:**

- Future NGINX releases (1.30.x, 1.31.x, etc.) MAY introduce additional registry entries (new RFC 9110 codes, new NGINX extensions). The set of `NGX_HTTP_*` macros may grow but never shrink.
- The `--with-http_status_validation` flag's strict-mode behavior may be refined in patch releases (e.g., adding new violation classes that are detected). Modules that compile against strict mode SHOULD test against the latest NGINX patch release.
- The internal layout of `ngx_http_status_def_t` (fields and field order) is **not** part of the public ABI. Modules MUST NOT rely on `sizeof(ngx_http_status_def_t)` or field-offset arithmetic. Use `ngx_http_status_register()` to add custom codes.

## Before/After Patterns

The migration translates direct field-assignment statements into API calls. The exact translation depends on the nature of the status value (compile-time constant vs. runtime variable) and on the surrounding error-handling context.

Six canonical patterns cover every direct-assignment site observed in the in-tree refactor:

1. [Pattern 1](#pattern-1-compile-time-constant-status--fire-and-forget) — Compile-time constant status (fire-and-forget)
2. [Pattern 2](#pattern-2-runtime-variable-status--defensive-error-return) — Runtime variable status (defensive error return)
3. [Pattern 3](#pattern-3-the-verbatim-aap-example) — The verbatim AAP example
4. [Pattern 4](#pattern-4-read-only-comparison--unchanged) — Read-only comparison (no migration)
5. [Pattern 5](#pattern-5-upstream-pass-through--special-case) — Upstream pass-through (special case)
6. [Pattern 6](#pattern-6-memzero--do-not-convert) — Memzero (do **not** convert)

### Pattern 1: Compile-Time Constant Status — Fire-and-Forget

The simplest migration: replace direct assignment of a known-good `NGX_HTTP_*` constant with the API call. The compiler will inline the function body and fold the in-range check to a no-op in permissive builds, yielding identical generated code.

**Before:**

```c
static ngx_int_t
my_module_handler(ngx_http_request_t *r)
{
    /* ... handler logic ... */

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = body_len;

    return ngx_http_send_header(r);
}
```

**After:**

```c
static ngx_int_t
my_module_handler(ngx_http_request_t *r)
{
    /* ... handler logic ... */

    (void) ngx_http_status_set(r, NGX_HTTP_OK);
    r->headers_out.content_length_n = body_len;

    return ngx_http_send_header(r);
}
```

**Notes:**

- The `(void)` cast acknowledges that the return value is intentionally ignored. For a compile-time constant in the 100–599 range, `ngx_http_status_set()` cannot fail.
- In permissive builds, the compiler folds the range check `100 <= NGX_HTTP_OK && NGX_HTTP_OK <= 599` to `true` at compile time, eliminates the conditional, and reduces the function call to a single `mov` instruction. Performance is identical to direct assignment.
- This pattern matches the in-tree conversion at `src/http/modules/ngx_http_static_module.c:229` (was: `r->headers_out.status = NGX_HTTP_OK;`).
- Other in-tree examples that follow Pattern 1: `src/http/modules/ngx_http_stub_status_module.c:137`, `src/http/modules/ngx_http_not_modified_filter_module.c:94`, `src/http/modules/ngx_http_range_filter_module.c:234,618`.

### Pattern 2: Runtime Variable Status — Defensive Error Return

When the status value comes from a runtime computation (parsed from input, derived from upstream, or otherwise variable), use the full error-check pattern. This is the canonical AAP § 0.1.2 example.

**Before:**

```c
static ngx_int_t
my_runtime_status_handler(ngx_http_request_t *r)
{
    ngx_uint_t  status;

    /* ... compute status from runtime input ... */
    status = compute_status_from_input(r);

    r->headers_out.status = status;
    return ngx_http_send_header(r);
}
```

**After (verbatim from AAP § 0.1.2):**

```c
static ngx_int_t
my_runtime_status_handler(ngx_http_request_t *r)
{
    ngx_uint_t  status;

    /* ... compute status from runtime input ... */
    status = compute_status_from_input(r);

    if (ngx_http_status_set(r, status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "invalid status code: %ui", status);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return ngx_http_send_header(r);
}
```

**Notes:**

- The error return is `NGX_HTTP_INTERNAL_SERVER_ERROR` (500) per AAP § 0.8.7 error-handling protocol — fall back gracefully, do not crash or terminate the worker.
- In permissive builds, `ngx_http_status_set()` returns `NGX_OK` for any value in 100–599 and logs a single debug line for out-of-range values; it does NOT return `NGX_ERROR` for in-range codes. The error return path is effectively a no-op until strict mode is enabled.
- In strict builds, `NGX_ERROR` is returned for codes outside 100–599, allowing the caller to fall back to 500 instead of emitting a malformed status line.
- This pattern matches the in-tree conversion at `src/http/ngx_http_core_module.c:1781` (was: `r->headers_out.status = status;`). Similar runtime-variable sites exist at `src/http/ngx_http_request.c:2838,3915` and `src/http/modules/ngx_http_dav_module.c:296`.

### Pattern 3: The Verbatim AAP Example

The following code block is the canonical migration example from AAP § 0.1.2 — preserved exactly:

```c
// OLD PATTERN (deprecated):
r->headers_out.status = NGX_HTTP_NOT_FOUND;

// NEW PATTERN (required):
if (ngx_http_status_set(r, 404) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: 404");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

**Note on terminology:** "Deprecated" in the OLD comment refers to a *coding-style preference* in NGINX core, not an ABI deprecation. Direct field assignment is **not** deprecated as ABI. Per AAP R-6, `r->headers_out.status = X;` continues to compile and run forever. Pattern 3 demonstrates the recommended new-code style; existing code is not under any obligation to adopt it. See [Pitfall 4](#pitfall-4-treating-migration-as-abi-deprecation) for further discussion.

### Pattern 4: Read-Only Comparison — Unchanged

If your module reads the status (for example, to make a decision about further processing), no migration is required. Read-only access is unchanged.

**No migration needed:**

```c
static ngx_int_t
my_filter_handler(ngx_http_request_t *r)
{
    if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
        /* skip body filter for 304 responses */
        return ngx_http_next_body_filter(r, NULL);
    }

    /* ... continue normal processing ... */
    return ngx_http_next_header_filter(r);
}
```

The new API is for *write* paths. Read paths remain identical. This matches the in-tree behavior in dozens of files (e.g., `src/http/ngx_http_upstream.c`, `src/http/modules/ngx_http_proxy_module.c`, `src/http/modules/ngx_http_fastcgi_module.c`). Migrating a read site to "use" the API would be incorrect — there is no `ngx_http_status_get()` function because the field itself is the source of truth on the read side.

### Pattern 5: Upstream Pass-Through — Special Case

If your module copies a status code from an upstream response (proxy, fastcgi, uwsgi, scgi, grpc, or a custom upstream protocol), you can convert the assignment but the API will detect the upstream context and bypass strict validation automatically.

**Before:**

```c
static ngx_int_t
my_upstream_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_t  *u = r->upstream;

    /* ... receive and parse upstream response ... */

    r->headers_out.status = u->headers_in.status_n;
    return ngx_http_send_header(r);
}
```

**After:**

```c
static ngx_int_t
my_upstream_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_t  *u = r->upstream;

    /* ... receive and parse upstream response ... */

    /* r->upstream != NULL is detected by the API; strict validation is bypassed.
       Even codes like 999 from a misbehaving upstream are passed through verbatim,
       matching pre-refactor behavior and the proxy_intercept_errors directive contract. */
    (void) ngx_http_status_set(r, u->headers_in.status_n);

    return ngx_http_send_header(r);
}
```

**Notes:**

- The upstream-bypass branch in `ngx_http_status_set()` is unconditional: if `r->upstream != NULL`, the assignment proceeds without range-check or strict-mode validation. This satisfies AAP D-006 ("Status code validation applies only to nginx-originated responses, not proxied responses").
- The `$upstream_status` access-log variable (preserved by the refactor) continues to expose the original upstream code for log analysis, even when `$status` reflects a different post-masquerade value.
- This pattern matches the in-tree conversion at `src/http/ngx_http_upstream.c:3165` (was: `r->headers_out.status = u->headers_in.status_n;`).

### Pattern 6: Memzero — Do NOT Convert

Some NGINX code uses `ngx_memzero(&r->headers_out, sizeof(ngx_http_headers_out_t));` or similar bulk-clear patterns. This is **not** an lvalue write of `r->headers_out.status` — it takes the address of the field for memset. Do **not** convert this.

```c
/* This is correct as-is; do NOT change to ngx_http_status_set */
ngx_memzero(&r->headers_out, sizeof(ngx_http_headers_out_t));
```

A grep that catches actual conversion targets is:

```bash
# Match lvalue assignments only; the [^=] excludes "==" comparisons
grep -rn 'r->headers_out\.status\s*=[^=]' src/
```

The pattern `r->headers_out.status\s*==[^=]` (note the doubled `=`) is read-only comparison and is also not converted (see [Pattern 4](#pattern-4-read-only-comparison--unchanged)).

## Step-by-Step Migration

The following procedure migrates a single third-party module file from direct assignment to the API. The procedure is per-call-site idempotent — you can migrate file by file, or all at once, in any order.

### Step 1: Verify NGINX Version Compatibility

The new API is available in NGINX 1.29.5 and later. To compile against multiple NGINX versions, use a feature-detection macro. Add this guard near the top of your source file:

```c
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* Feature detection: ngx_http_status_set was introduced in 1.29.5.
   The cleanest signal is the presence of <ngx_http_status.h>, which ngx_http.h
   pulls in transitively in 1.29.5+. We use a sentinel macro defined in that header. */
#if defined(NGX_HTTP_STATUS_CACHEABLE)
#  define MY_MODULE_HAS_STATUS_API 1
#else
#  define MY_MODULE_HAS_STATUS_API 0
#endif
```

Then guard the migrated calls:

```c
#if MY_MODULE_HAS_STATUS_API
    (void) ngx_http_status_set(r, NGX_HTTP_OK);
#else
    r->headers_out.status = NGX_HTTP_OK;
#endif
```

If your module is internal-only and runs against a single pinned NGINX version, you can skip the guard and use the API directly.

### Step 2: Locate Direct-Assignment Call Sites

Use `grep` to find every direct write to `r->headers_out.status`:

```bash
# From your module's source root
grep -rn 'r->headers_out\.status\s*=[^=]' src/  # excludes == comparisons via [^=]
```

The exact pattern `r->headers_out\.status\s*=[^=]` matches:

- `r->headers_out.status = NGX_HTTP_OK;` (constant)
- `r->headers_out.status = status;` (variable)
- `r->headers_out.status = u->headers_in.status_n;` (upstream pass-through)

It excludes:

- `if (r->headers_out.status == NGX_HTTP_OK)` (read-only comparison)
- `ngx_memzero(&r->headers_out, sizeof(...));` (bulk memset)

For each match, classify it by the patterns documented in [Before/After Patterns](#beforeafter-patterns):

- **Pattern 1** (constant, no error path): use the fire-and-forget form.
- **Pattern 2** (runtime variable, error path needed): use the defensive error-return form.
- **Pattern 5** (upstream pass-through): use the fire-and-forget form; the API auto-detects upstream context via `r->upstream != NULL`.

### Step 3: Apply the Migration

For each call site, replace the direct assignment per the patterns. General guidelines:

| Original | Replacement |
|---|---|
| `r->headers_out.status = NGX_HTTP_OK;` | `(void) ngx_http_status_set(r, NGX_HTTP_OK);` |
| `r->headers_out.status = status;` (where `status` is in-range by construction) | `(void) ngx_http_status_set(r, status);` |
| `r->headers_out.status = status;` (where `status` could be invalid) | `if (ngx_http_status_set(r, status) != NGX_OK) { return NGX_HTTP_INTERNAL_SERVER_ERROR; }` |
| `r->headers_out.status = u->headers_in.status_n;` | `(void) ngx_http_status_set(r, u->headers_in.status_n);` (upstream context auto-detected) |
| `if (r->headers_out.status == NGX_HTTP_X)` | **No change** — read paths are unchanged |

### Step 4: Add Required Includes (if your module does not already include `<ngx_http.h>`)

Most NGINX modules already include `<ngx_http.h>`, which transitively includes `<ngx_http_status.h>` in NGINX 1.29.5+. No explicit `#include <ngx_http_status.h>` is needed in consumer code.

If your module is unusually minimal and does not include `<ngx_http.h>` directly, add it:

```c
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
```

This is the canonical NGINX module include trio. Every in-tree HTTP module begins with these three lines in this order.

### Step 5: Rebuild

Recompile your module against NGINX 1.29.5 sources:

```bash
cd /path/to/nginx-1.29.5
./auto/configure --add-module=/path/to/your/module
make -j$(nproc)
```

Verify a clean compile with no new warnings. The default NGINX `CFLAGS` includes `-Werror`, so any warning is fatal.

### Step 6: Run Functional Tests

Run your module's existing test suite against the rebuilt binary. Backward compatibility means **all previously passing tests should continue to pass without modification**.

If your module ships test fixtures that exercise edge-case status codes (e.g., 100, 599), confirm they still pass. If your module is exercised by the external `nginx-tests` Perl suite, point `TEST_NGINX_BINARY` at your rebuilt binary and run `prove -r t/`.

### Step 7: Optionally Validate Against Strict Mode

To gain confidence that the migration handles edge cases, rebuild with `--with-http_status_validation` and rerun the test suite:

```bash
./auto/configure --add-module=/path/to/your/module --with-http_status_validation
make -j$(nproc)
```

If your tests include scenarios where modules emit non-RFC-9110 status codes (rare but possible), strict mode will surface them. Address each warning individually:

- If the code is intentional, document why it's acceptable in your module's README and continue using the permissive default in production.
- If the code is a bug, fix it.

### Step 8: Verify Observability Integration

With `error_log /tmp/nginx-error.log debug;` in your test config, run a request that exercises the migrated code path:

```bash
curl -v http://localhost:8080/your-endpoint
tail /tmp/nginx-error.log | grep 'http status set'
```

Expected output: a line like `http status set: 200 OK (strict=no upstream=no)` with the correct numeric code and reason phrase. The `$request_id` correlation ID ties this debug line to the corresponding access-log entry. See [observability integration](../architecture/observability.md) for the recommended `log_format` and the data-source-agnostic Grafana dashboard template.

### Step 9: Commit Per Convention

Commit the migration in a single atomic change. The recommended commit message format:

```text
Migrate to ngx_http_status_set() API.

Converts direct r->headers_out.status assignments to the
centralized API introduced in NGINX 1.29.5. No functional
or wire-format changes; backward compatible with NGINX 1.29.4
and earlier via the MY_MODULE_HAS_STATUS_API guard.
```

Keep migration commits focused: do not bundle unrelated refactors. A reviewer should be able to verify the change is purely a syntactic substitution by reading the diff.

## Strict Validation Mode (`--with-http_status_validation`)

Per [decision D-005](../architecture/decision_log.md#architectural-decisions), the strict-mode validation flag is **off by default**. Operators opt in via the configure flag:

```bash
./auto/configure --with-http_status_validation
```

This defines the preprocessor symbol `NGX_HTTP_STATUS_VALIDATION=1` in `objs/ngx_auto_config.h`, which activates additional checks inside `ngx_http_status_set()`.

### What Changes Under Strict Mode

| Behavior | Permissive (default) | Strict (`--with-http_status_validation`) |
|---|---|---|
| Out-of-range code (`<100` or `>599`) | Logged at `NGX_LOG_DEBUG_HTTP`, still assigned, returns `NGX_OK` | Logged at `NGX_LOG_WARN`, NOT assigned, returns `NGX_ERROR` |
| 1xx code emitted after a 2xx–5xx final response on the same request | No detection; assigned silently | Logged at `NGX_LOG_ERR`, NOT assigned, returns `NGX_ERROR` |
| Multiple final-code assignments per request | Allowed (last wins, matching pre-refactor behavior) | Allowed in V1; reserved for future enhancement |
| Upstream pass-through (`r->upstream != NULL`) | Bypasses validation | Bypasses validation (same as permissive) |
| Compile-time-constant `NGX_HTTP_OK` etc. in valid range | No-op at runtime (compiler-folded) | No-op at runtime (compiler-folded) |
| Debug log line emitted on each call | Yes — `http status set: N <reason> (strict=no upstream=...)` | Yes — `http status set: N <reason> (strict=yes upstream=...)` |

### Module-Author Implications

If you are a module author whose code might be loaded into a strict-mode build:

1. **Always check the return value when the status is a runtime variable** — use [Pattern 2](#pattern-2-runtime-variable-status--defensive-error-return) or [Pattern 5](#pattern-5-upstream-pass-through--special-case) from the patterns section. Don't use `(void)` cast for runtime-variable cases.
2. **Don't emit codes outside `100..599`** unless you also document that your module is incompatible with strict mode.
3. **Don't emit a 1xx code after a final response** has already been sent for the same request. This is an HTTP protocol violation per RFC 9110 § 15.2.
4. **Test your module against both build flavors** to ensure forward compatibility with downstream NGINX builds that may have strict mode enabled.

If your module is for upstream proxying (your own custom protocol module), the upstream-bypass branch in `ngx_http_status_set()` automatically exempts your pass-through assignments from strict-mode checks. You need to ensure `r->upstream != NULL` at the time of assignment (which is true by construction in upstream protocol handlers — the `ngx_http_upstream_create()` call must precede status assignment).

### Why Strict Mode Is Off by Default

Off-by-default ensures backward compatibility for existing nginx.conf deployments where third-party modules may emit non-standard codes. Strict mode is opt-in for nginx builders who want RFC 9110 conformance enforced in their distribution.

If you maintain a downstream NGINX build (distribution package, custom enterprise build, security-hardened spin), you may choose to enable strict mode. Document this in your build's release notes so module authors know to test against your strict-mode binary.

### Performance Implications

In permissive builds, the new API has no measurable runtime cost relative to direct assignment for compile-time-constant arguments. The compiler folds the range check to a no-op and reduces the call to a single `mov` instruction. For runtime-variable arguments, expect a single comparison and conditional branch — measured at well under 10 CPU cycles per call on modern x86_64 hardware.

In strict builds, expect 30–60 additional CPU cycles per call for the registry lookup and extra checks. This is well below the AAP's 2% latency-overhead gate on `wrk -t4 -c100 -d30s` benchmarks. See [Performance Impact](../architecture/decision_log.md#performance-impact) for the full before/after numbers.

## Common Pitfalls

The following pitfalls are flagged from the project's [decision log](../architecture/decision_log.md#architectural-decisions) and from the AAP's behavioral prohibitions (§ 0.8.2). Avoid them in your migration.

### Pitfall 1: Calling `ngx_http_status_register()` Post-Init

The `ngx_http_status_register()` function is the only way to extend the registry with custom status codes (e.g., a vendor-specific 5xx code). However, it is callable **only during the configuration parsing phase** — typically from a module's `init_module` callback or a `preconfiguration` hook of an HTTP module.

**Wrong:**

```c
static ngx_int_t
my_module_handler(ngx_http_request_t *r)
{
    ngx_http_status_def_t  def = {
        .code = 599,
        .reason = ngx_string("My Custom Code"),
        .flags = NGX_HTTP_STATUS_NGINX_EXT,
        .rfc_section = "vendor extension"
    };

    /* WRONG: called per-request after worker fork — returns NGX_ERROR */
    if (ngx_http_status_register(&def) != NGX_OK) {
        /* this branch is hit on every call after the first request */
    }

    /* ... handler logic ... */
    return NGX_OK;
}
```

**Right:**

```c
static ngx_int_t
my_module_init(ngx_conf_t *cf)
{
    ngx_http_status_def_t  def = {
        .code = 599,
        .reason = ngx_string("My Custom Code"),
        .flags = NGX_HTTP_STATUS_NGINX_EXT,
        .rfc_section = "vendor extension"
    };

    /* Right: called during configuration parsing; registry is mutable here. */
    if (ngx_http_status_register(&def) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "failed to register custom status code 599");
        return NGX_ERROR;
    }

    return NGX_OK;
}
```

This restriction satisfies the AAP behavioral prohibition "Never create registry modification APIs accessible post-initialization" — the registry is read-only after worker fork to satisfy thread-safety-by-immutability per [decision D-004](../architecture/decision_log.md#architectural-decisions).

### Pitfall 2: Confusing Permissive and Strict Return Semantics

`ngx_http_status_set()` returns `NGX_OK` (== 0) on success and `NGX_ERROR` (== -1) on failure. The exact set of failure cases depends on the build mode:

- **Permissive (default):** Only fails for utterly malformed inputs (e.g., the `r` argument is `NULL`, which should never happen in correctly-written code). Out-of-range codes log at DEBUG but still return `NGX_OK`.
- **Strict (`--with-http_status_validation`):** Fails for out-of-range codes, 1xx-after-final violations, and other RFC 9110 conformance issues.

If you compile your module against the permissive build but your users deploy to a strict build, your module's runtime behavior will differ. Always handle the `NGX_ERROR` return path correctly even if you don't expect it to fire on your test infrastructure. The cost of a defensive check is one branch instruction; the cost of a missing check is a malformed status line on the wire.

### Pitfall 3: Forgetting the `r->upstream` Bypass

The API auto-detects upstream context via `r->upstream != NULL`. This bypass exists for a reason: NGINX MUST NOT alter the status code received from a backend, even if the backend emits a non-RFC-9110 code (e.g., a legacy application returning `1000` or `0`). This is a contract obligation per [decision D-006](../architecture/decision_log.md#architectural-decisions).

If you write a custom upstream protocol module, ensure you set `r->upstream` to a non-`NULL` `ngx_http_upstream_t *` before calling `ngx_http_status_set()`. If you forget, strict mode will reject your upstream-derived codes. The standard pattern (used by `ngx_http_proxy_module.c`, `ngx_http_fastcgi_module.c`, `ngx_http_uwsgi_module.c`, `ngx_http_scgi_module.c`, `ngx_http_grpc_module.c`) calls `ngx_http_upstream_create()` early in the handler chain, well before any status assignment.

### Pitfall 4: Treating "Migration" as ABI Deprecation

The OLD/NEW pattern blocks in this guide use the words "deprecated" and "required" to express the *coding-style preference* for new code in NGINX core. They do **not** indicate ABI deprecation. Direct field assignment (`r->headers_out.status = X;`) is **not** removed, hidden, or scheduled for removal. It compiles forever per AAP R-6.

Do not refactor mature, stable, well-tested module code "just because." The migration's value comes from validation, observability, and forward-compatibility — not from chasing the latest API style. If your module has shipped to production for years and is functioning correctly, the cost of touching every call site (with the associated risk of introducing a regression) usually exceeds the benefit of the migration. Defer until your next planned release window.

### Pitfall 5: Reading the Reason Phrase Where the Wire Reason Is Different

`ngx_http_status_reason(code)` returns the **canonical RFC 9110 phrase** for the code. The on-wire HTTP/1.1 reason phrase is governed by `ngx_http_status_lines[]` in `src/http/ngx_http_header_filter_module.c`, which is **preserved byte-for-byte** for backward compatibility. For 6 codes (302, 408, 413, 414, 416, 503), the registry reason and the wire reason differ. See [decision D-008](../architecture/decision_log.md#architectural-decisions).

The divergent codes:

| Code | Wire Reason (preserved) | Registry Reason (RFC 9110) |
|---|---|---|
| 302 | `Moved Temporarily` | `Found` |
| 408 | `Request Time-out` | `Request Timeout` |
| 413 | `Request Entity Too Large` | `Content Too Large` |
| 414 | `Request-URI Too Large` | `URI Too Long` |
| 416 | `Requested Range Not Satisfiable` | `Range Not Satisfiable` |
| 503 | `Service Temporarily Unavailable` | `Service Unavailable` |

If your module needs the wire reason (for example, to log it with the same wording the client receives), do NOT use `ngx_http_status_reason()`. Instead, accept that the registry returns the canonical RFC 9110 phrase, and consult the wire-table source directly in your own code if you need byte-exact wire reason text.

### Pitfall 6: Trying to Build Per-Code Counters

The AAP behavioral prohibition "Never introduce caching mechanisms for status code lookups beyond direct array indexing" rules out building in-core per-code counters (e.g., a shared-memory atomic increment per status code) into the registry itself. Instead, use the existing `$status` access-log variable and aggregate post-hoc with your log-aggregation tooling (Loki, Elasticsearch, CloudWatch Logs, Splunk, etc.). See [observability integration](../architecture/observability.md) for the recommended log format and Grafana dashboard template.

If you genuinely need real-time per-code counters in NGINX itself (rare), implement them in your own module using the standard NGINX shared-memory zone (`ngx_shm_zone_t`) primitives, completely independent of the status-code registry. The registry must remain a static, immutable, lookup-only data structure.

### Pitfall 7: Trying to Alias Status Codes

The AAP behavioral prohibition "Never add status code aliasing functionality" rules out registering two different reasons for the same numeric code. The registry maps each code to exactly one reason phrase. If your module needs multiple semantically distinct meanings for a single numeric code, encode that distinction in another field (e.g., a custom response header) instead of trying to alias the status.

For example, do NOT attempt:

```c
/* WRONG: attempt to register a second meaning for 503 */
ngx_http_status_def_t  def = {
    .code = 503,
    .reason = ngx_string("Database Unavailable"),  /* alternative to "Service Unavailable" */
    /* ... */
};
ngx_http_status_register(&def);  /* will return NGX_ERROR — duplicate code */
```

Instead, emit `503` with the canonical phrase and surface the operational distinction via a custom header (`X-Failure-Reason: database`) or a structured response body.

### Pitfall 8: Modifying the `error_page` Directive Behavior

The AAP § 0.8.5 preservation mandate forbids any modification to `error_page`, `return`, or `proxy_intercept_errors` directive parsing or behavior. The `error_page` parser (function `ngx_http_core_error_page`) at `src/http/ngx_http_core_module.c:4914-5029` is preserved byte-for-byte. Do NOT attempt to extend `error_page` semantics by hooking into the new API; the registry is for status-code metadata only, not for error-page mapping. If your module needs custom error-page handling, write your own directive that delegates to your own table.

## Verification Checklist

After completing the migration, verify each item below to confirm a successful adoption.

| # | Check | Command / Action | Expected Result |
|---|---|---|---|
| 1 | Module compiles cleanly | `./auto/configure --add-module=/path/to/your/module && make` | No new warnings or errors; the default `-Werror` build flag passes |
| 2 | Existing tests pass | Run your module's test suite | All pre-migration tests pass without modification |
| 3 | No remaining direct assignments | `grep -rn 'r->headers_out\.status\s*=[^=]' your_module/` | Output is empty (zero remaining lvalue writes), OR all remaining writes are documented exceptions (e.g., upstream pass-through preserved by intent) |
| 4 | Upstream pass-through preserved | If your module proxies, send a non-standard upstream code (e.g., 999) and confirm it reaches the wire | Wire response carries 999; `$status` access-log variable shows 999 |
| 5 | Read-only access unchanged | `grep -rn 'r->headers_out\.status\s*==' your_module/` | Output unchanged from before the migration |
| 6 | Strict-mode build also compiles | `./auto/configure --add-module=/path/to/your/module --with-http_status_validation && make` | No new warnings or errors |
| 7 | Strict-mode tests pass (or document exceptions) | Re-run test suite against the strict-mode binary | All tests pass, OR each failure has a documented justification |
| 8 | Debug trace appears | With `error_log /tmp/error.log debug;`, exercise your handler and `tail /tmp/error.log` | Lines containing `http status set: <N> <reason> (strict=... upstream=...)` |
| 9 | `$status` correlates | Ensure access log shows the same status code as the debug trace via `$request_id` correlation | The numeric code in `$status` matches the numeric code in the debug trace for the same `request_id` |
| 10 | Module load test (if shipped as a `.so`) | Build module as dynamic, load via `load_module modules/my_module.so;` | NGINX starts cleanly; module functions identically |
| 11 | Wire format unchanged | `curl -i http://localhost:8080/your-endpoint` before and after migration | Status line bytes are byte-identical between pre-migration and post-migration responses |
| 12 | No new memory leaks | `valgrind --leak-check=full ./objs/nginx -g 'daemon off;'` while exercising your handler | Zero new "definitely lost" or "indirectly lost" leaks attributable to your code |

A migration is complete when all 12 checks pass. Item 3 is the most common failure mode — leftover `r->headers_out.status = ...` lvalue writes that were missed during the audit. Re-run the grep until the output is clean (or every remaining line has a documented justification).

## FAQ

**Q: Do I have to migrate?**

A: No. Migration is optional and forever. Direct field assignment continues to compile and run identically. See [Backward Compatibility Guarantees](#backward-compatibility-guarantees).

**Q: Will my pre-built `.so` module from NGINX 1.29.4 still load on NGINX 1.29.5?**

A: Yes. The module ABI (`NGX_MODULE_V1`) is unchanged. Your binary loads as-is.

**Q: Will my source code that uses `r->headers_out.status = NGX_HTTP_OK;` still compile on NGINX 1.29.5?**

A: Yes. The field is preserved, the macro is preserved, and the assignment statement is grammatically valid C. No source change required.

**Q: I want to migrate, but my module supports both NGINX 1.29.4 and 1.29.5. How?**

A: Use the `MY_MODULE_HAS_STATUS_API` feature-detection guard from [Step 1](#step-1-verify-nginx-version-compatibility). The new API symbol `NGX_HTTP_STATUS_CACHEABLE` is defined only in 1.29.5+ headers.

**Q: Why are there two reason phrases for some status codes?**

A: For 6 codes (302, 408, 413, 414, 416, 503), NGINX has historically emitted a wire reason that pre-dates RFC 9110's canonical wording. The wire table is preserved for backward compatibility (some HTTP clients may parse on the reason text). The registry carries the RFC 9110 canonical phrase for future use. See [decision D-008](../architecture/decision_log.md#architectural-decisions) and [Pitfall 5](#pitfall-5-reading-the-reason-phrase-where-the-wire-reason-is-different).

**Q: My module emits a non-standard code like 999 for testing. Will it still work?**

A: In permissive (default) builds, yes — `ngx_http_status_set(r, 999)` logs at DEBUG and assigns the code. In strict builds, it returns `NGX_ERROR`. Either avoid strict mode for your test environment or skip the migration for code paths that emit non-standard codes.

**Q: Can I register a new status code at runtime?**

A: Only during the configuration phase, via `ngx_http_status_register()` from a module init or preconfiguration callback. After worker fork, the registry is read-only. See [Pitfall 1](#pitfall-1-calling-ngx_http_status_register-post-init).

**Q: My module has 200+ direct-assignment sites. Should I migrate all at once?**

A: Up to you. The migration is per-call-site idempotent — you can migrate file by file or all at once. NGINX core migrated all in-tree call sites in a single commit (~17 in-scope sites across ~14 files); your timing can be different.

**Q: Is the new API faster, slower, or the same as direct assignment?**

A: Same in permissive builds. The compiler inlines `ngx_http_status_set()` for compile-time-constant arguments and folds the range check to a no-op. Generated assembly is byte-identical to direct assignment in optimized builds. In strict builds, expect 30–60 CPU cycles of overhead per call (registry lookup + extra checks). See [Performance Impact](../architecture/decision_log.md#performance-impact).

**Q: Does this affect HTTP/2 or HTTP/3?**

A: No. HTTP/2 and HTTP/3 emit the numeric `:status` pseudo-header via HPACK or QPACK, respectively. They never read the reason phrase. The refactor's reason-phrase lookup is an HTTP/1.1-only concern. See [architecture diagrams Fig-3](../architecture/status_code_refactor.md#fig-3--request-lifecycle-status-code-flow).

**Q: Do I need to include `<ngx_http_status.h>` explicitly?**

A: No. `<ngx_http.h>` includes `<ngx_http_status.h>` transitively in NGINX 1.29.5+. Any module that already includes `<ngx_http.h>` (which is essentially every HTTP module in existence) gets the API and types for free.

**Q: What happens when I call `ngx_http_status_set(r, NGX_HTTP_OK)` if `r` is `NULL`?**

A: The function is defensive against `NULL` `r` and returns `NGX_ERROR` without dereferencing. In practice this never happens in correctly-written code — every NGINX HTTP handler receives a non-`NULL` `ngx_http_request_t *`.

**Q: My module writes to `r->headers_out.status` and to `r->err_status` separately. Does the API affect `r->err_status`?**

A: No. The new API only mediates writes to `r->headers_out.status`. The `r->err_status` field (used by NGINX core for the original pre-masquerade status, e.g., to drive `error_page` lookup and `$status` reporting for codes 494/495/496/497) is unchanged. If your module reads or writes `r->err_status`, no migration is needed for those sites.

**Q: Where do I report bugs in the new API?**

A: Use the standard NGINX bug-reporting channels (the `nginx-devel` mailing list). Include a minimal reproducer, the NGINX version (`./objs/nginx -V`), and the configure flags used.

**Q: Will future NGINX versions remove direct field assignment?**

A: No plans. Per the project's permanent backward-compatibility commitment (AAP R-6), the field will continue to exist. If a future major version were to remove it, that would be a breaking change announced in advance.

## See Also

- [`../api/status_codes.md`](../api/status_codes.md) — Full API reference for all five public functions, including parameter and return-value semantics
- [`../architecture/decision_log.md`](../architecture/decision_log.md) — Architectural decisions and bidirectional traceability matrix; especially [D-005](../architecture/decision_log.md#architectural-decisions) (off-by-default validation), [D-006](../architecture/decision_log.md#architectural-decisions) (upstream pass-through), and [D-008](../architecture/decision_log.md#architectural-decisions) (registry vs. wire-table reason-phrase divergence)
- [`../architecture/status_code_refactor.md`](../architecture/status_code_refactor.md) — Mermaid before/after diagrams (Fig-1 through Fig-5)
- [`../architecture/observability.md`](../architecture/observability.md) — Observability integration: recommended `log_format`, Grafana dashboard JSON template, local-environment verifiability checklist
- [RFC 9110 § 15 — HTTP Status Codes](https://www.rfc-editor.org/rfc/rfc9110.html#name-status-codes) — Authoritative HTTP semantics specification
- [IANA HTTP Status Code Registry](https://www.iana.org/assignments/http-status-codes/http-status-codes.xml) — Authoritative registered-codes list
- [NGINX development guide](https://nginx.org/en/docs/dev/development_guide.html) — Coding conventions for NGINX module authors
- NGINX source files referenced as live migration examples:
  - `src/http/modules/ngx_http_static_module.c:229` — Pattern 1 (`NGX_HTTP_OK` constant)
  - `src/http/modules/ngx_http_autoindex_module.c:258` — Pattern 1 (`NGX_HTTP_OK` constant on directory-listing serve)
  - `src/http/modules/ngx_http_stub_status_module.c:137` — Pattern 1 (`NGX_HTTP_OK` constant)
  - `src/http/modules/ngx_http_not_modified_filter_module.c:94` — Pattern 1 (`NGX_HTTP_NOT_MODIFIED` 304 constant)
  - `src/http/modules/ngx_http_range_filter_module.c:234` — Pattern 1 (`NGX_HTTP_PARTIAL_CONTENT` 206 constant)
  - `src/http/modules/ngx_http_range_filter_module.c:618` — Pattern 1 (`NGX_HTTP_RANGE_NOT_SATISFIABLE` 416 constant)
  - `src/http/ngx_http_core_module.c:1781` — Pattern 2 (runtime variable `status`; canonical early-return AAP § 0.1.2 R3 pattern)
  - `src/http/ngx_http_core_module.c:1883` — Pattern 2 variant (runtime variable `r->err_status`; uses fallback-assign of `NGX_HTTP_INTERNAL_SERVER_ERROR` per AAP § 0.8.7 instead of early-return because the calling context — `ngx_http_send_header()` — must continue to invoke `ngx_http_top_header_filter(r)`)
  - `src/http/ngx_http_request.c:2838` — Pattern 2 (runtime variable in subrequest copy-through)
  - `src/http/ngx_http_request.c:3915` — Pattern 2 (runtime variable in lifecycle finalize)
  - `src/http/ngx_http_upstream.c:3165` — Pattern 5 (upstream pass-through `u->headers_in.status_n`)
  - `src/http/modules/ngx_http_dav_module.c:296` — Pattern 2 (runtime variable `status`)
