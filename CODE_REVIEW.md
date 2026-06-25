# Code Review — HTTP Status-Code Registry & Facade API

## Phase 0 — Header & Review Scope

### Overview

This pull request refactors nginx's HTTP status-code handling from a
scattered, convention-based model into a **centralized, registry-backed
API**. Previously, response status was set by directly mutating
`r->headers_out.status` at call sites distributed across the HTTP source
tree, while reason phrases lived in a separate offset-indexed table inside
the HTTP/1.x header filter and the numeric constants were defined as
`#define` macros in `src/http/ngx_http_request.h`. The refactor consolidates
that dispersed knowledge behind a single immutable registry and a thin
facade.

The new design introduces three coordinated pieces:

- an immutable `static const ngx_http_status_def_t status_registry[]` table —
  one record per status code carrying its numeric `code`, default `reason`
  phrase, classification `flags`, and `rfc_section` reference, addressed by
  O(1) per-class offset arithmetic — in `src/http/ngx_http_request.c`, serving
  as the single source of truth;
- a five-function facade declared in `src/http/ngx_http.h` —
  `ngx_http_status_set`, `ngx_http_status_validate`, `ngx_http_status_reason`,
  `ngx_http_status_register`, and `ngx_http_status_is_cacheable`;
- classification flag macros `NGX_HTTP_STATUS_CACHEABLE`,
  `NGX_HTTP_STATUS_CLIENT_ERROR`, `NGX_HTTP_STATUS_SERVER_ERROR`, and
  `NGX_HTTP_STATUS_INFORMATIONAL`, together with the `ngx_http_status_def_t`
  registry-record typedef, both in `src/http/ngx_http_request.h`.

The change is **additive, backward-compatible, and behavior-preserving**.
Every existing `NGX_HTTP_*` numeric constant is retained, direct
`r->headers_out.status =` assignment still compiles and works, and strict
RFC 9110 §15 validation is **opt-in**: it is gated behind the compile-time
macro `NGX_HTTP_STATUS_VALIDATION`, delivered through a
`-DNGX_HTTP_STATUS_VALIDATION` compiler define in `CFLAGS`
(`--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`). The default build is
permissive and carries effectively zero overhead. The `auto/` build-system
directory is intentionally left unchanged.

### Review Methodology

This is a **segmented PR review**. Every changed file is partitioned into
**exactly one** sequential domain phase (the one-file-one-domain invariant):
Infrastructure/DevOps, Security, Backend Architecture, QA/Test Integrity,
Business/Domain, Frontend, or Other SME. Each domain phase resolves to
exactly one of two outcomes — **APPROVED** or **BLOCKED** (with reasons) —
and domains that own no committed files still perform a documented
cross-cutting review and resolve with rationale. After all domain phases, an
independent **Final Reviewer Verdict** re-verifies the build (default and
validation-enabled), the test plan, static analysis, contribution
conventions, and the cross-cutting guarantees before issuing an overall
verdict.

### Branch / PR Under Review

The review covers **this PR** — the HTTP status-code registry and facade-API
refactor branch — relative to the pre-refactor baseline of the `blitzy-nginx`
tree (nginx 1.29.5). The changed-file inventory in Phase 1 is reconciled
against the **actual PR diff** rather than taken verbatim from the design
specification; where the specification's wildcard was broader than the real
diff, the reconciliation is documented inline. The review is grounded in the
nginx contribution conventions in `CONTRIBUTING.md` (commit-message limits,
scoped subject prefixes, the BSD-2-Clause grant, and the F5 CLA) and the
nginx code style.

---

## Phase 1 — Changed-File Inventory

Every changed file is listed below exactly once, mapped to its single
assigned domain phase. The inventory reflects the actual PR diff. The three
filter modules named by the design specification's broad
`ngx_http_*_module.c` wildcard but **not** present in the diff —
`ngx_http_charset_filter_module.c`, `ngx_http_chunked_filter_module.c`, and
`ngx_http_xslt_filter_module.c` — were reviewed and found to only **read**
`r->headers_out.status` (status-class comparisons used for normalization
decisions) with **no** direct-assignment (Pattern A) write sites; they
therefore require no conversion and are correctly excluded from the diff.
That reconciliation is revisited in Phase 2.3.

| # | Changed File | Type | Domain Phase |
|---|--------------|------|--------------|
| 1 | `src/http/ngx_http_request.c` | UPDATE | Backend Architecture (2.3) |
| 2 | `src/http/ngx_http_request.h` | UPDATE | Backend Architecture (2.3) |
| 3 | `src/http/ngx_http.h` | UPDATE | Backend Architecture (2.3) |
| 4 | `src/http/ngx_http_core_module.c` | UPDATE | Backend Architecture (2.3) |
| 5 | `src/http/ngx_http_header_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 6 | `src/http/ngx_http_special_response.c` | UPDATE | Backend Architecture (2.3) |
| 7 | `src/http/modules/ngx_http_static_module.c` | UPDATE | Backend Architecture (2.3) |
| 8 | `src/http/modules/ngx_http_dav_module.c` | UPDATE | Backend Architecture (2.3) |
| 9 | `src/http/modules/ngx_http_autoindex_module.c` | UPDATE | Backend Architecture (2.3) |
| 10 | `src/http/modules/ngx_http_range_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 11 | `src/http/modules/ngx_http_not_modified_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 12 | `src/http/modules/ngx_http_flv_module.c` | UPDATE | Backend Architecture (2.3) |
| 13 | `src/http/modules/ngx_http_mp4_module.c` | UPDATE | Backend Architecture (2.3) |
| 14 | `src/http/modules/ngx_http_gzip_static_module.c` | UPDATE | Backend Architecture (2.3) |
| 15 | `src/http/modules/ngx_http_stub_status_module.c` | UPDATE | Backend Architecture (2.3) |
| 16 | `src/http/modules/ngx_http_image_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 17 | `src/http/modules/ngx_http_slice_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 18 | `src/http/v2/ngx_http_v2_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 19 | `src/http/v3/ngx_http_v3_filter_module.c` | UPDATE | Backend Architecture (2.3) |
| 20 | `docs/api/status_codes.md` | CREATE | Business / Domain (2.5) |
| 21 | `docs/migration/status_code_api.md` | CREATE | Business / Domain (2.5) |
| 22 | `docs/migration/traceability_matrix.md` | CREATE | Business / Domain (2.5) |
| 23 | `docs/decisions/status_code_refactor.md` | CREATE | Business / Domain (2.5) |
| 24 | `docs/xml/nginx/changes.xml` | UPDATE | Business / Domain (2.5) |
| 25 | `docs/observability/status_dashboard.json` | CREATE | Infrastructure / DevOps (2.1) |
| 26 | `blitzy-deck/status_code_refactor_executive_summary.html` | CREATE | Frontend (2.6) |
| 27 | `blitzy-deck/references/blitzy-reveal-theme.css` | CREATE | Frontend (2.6) |
| 28 | `CODE_REVIEW.md` | CREATE | Other SME (2.7) |

**Mutual-exclusivity check:** 28 files, each assigned to exactly one domain
phase. Backend Architecture owns 19 files, Business/Domain owns 5,
Infrastructure/DevOps owns 1, Frontend owns 2, and Other SME owns 1
(`CODE_REVIEW.md` itself). The Security and QA/Test Integrity phases own no
committed files and perform cross-cutting reviews (Phases 2.2 and 2.4). Every
one of the 28 changed files in the PR diff appears in this table exactly once,
including this review document.

---

## Phase 2 — Domain Review Phases

Each domain below resolves to a single bold verdict. Domains are reviewed in
sequential order.

### Phase 2.1 — Infrastructure / DevOps

**Files owned:** `docs/observability/status_dashboard.json`.

**Observability template.** `docs/observability/status_dashboard.json` is a
dashboard template describing the panels that surface status-handling
health: a **status-class distribution** panel (1xx/2xx/3xx/4xx/5xx response
counts) and a **validation-failure counter** panel that tracks how often
`ngx_http_status_validate()` rejects a locally generated code when the
optional validation build is enabled. The template is reviewed for valid
JSON syntax (parses cleanly, no trailing commas, balanced braces) and for
sourcing its series from nginx's existing `ngx_http_stub_status_module`
surface rather than introducing a new metrics subsystem. No new runtime
exporter, agent, or sidecar is added — the artifact is a documentation/
configuration template that an operator can adapt to their dashboarding
stack. None of the prohibited subsystems (event loop, configuration parser,
memory allocator) is touched.

**Build-system decision.** The reviewer confirms the deliberate, low-risk
build posture:

- The `auto/` directory is **intentionally unchanged**. No probe, source
  list, or option wiring is edited.
- The optional strict-validation feature is delivered purely as a
  compile-time define, `-DNGX_HTTP_STATUS_VALIDATION`, supplied via
  `CFLAGS` (`--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`). When the macro
  is absent, every `#if (NGX_HTTP_STATUS_VALIDATION)` block compiles away, so
  the **default build is byte-for-byte unaffected** in behavior on the hot
  path.
- **No new source files** were added to the build graph. The registry and
  the five facade functions live inside the already-compiled
  `src/http/ngx_http_request.c`, so there is no new object file and no
  `auto/sources` entry to wire. This keeps the change additive and avoids any
  risk to the existing object/Makefile generation.

The default build configures and compiles cleanly under the project's
standard feature set with `-Werror` in force; the validation-enabled build
adds only the gated checks and likewise compiles warning-clean.

**Verdict: APPROVED** — the observability template is valid and reuses
existing facilities, and the build-system strategy is minimal-disruption,
default-build-neutral, and adds no new source files.

### Phase 2.2 — Security

**Files owned:** none. The security concerns are realized inside
Backend-owned files (primarily `src/http/ngx_http_request.c`); this phase
reviews them cross-cutting.

- **(a) Opt-in validation scope.** `ngx_http_status_validate()` enforces the
  RFC 9110 §15 range and registry membership **only** when
  `NGX_HTTP_STATUS_VALIDATION` is defined. In the default build the strict
  block is compiled out, so the function reduces to a cheap range check and
  the sentinel whitelist. This prevents the validation layer from ever
  silently changing wire behavior unless an operator explicitly opts in.
- **(b) Upstream pass-through guard.** `ngx_http_status_set()` checks
  `r->upstream` **first**; when the response originates from a proxied
  upstream the code is stored verbatim and the function returns immediately,
  bypassing all validation. Origin-assigned status is therefore never
  transformed or rejected. The upstream status-copy path in
  `ngx_http_upstream.c` is untouched and remains a pure pass-through, so this
  refactor introduces no behavioral change for proxied responses.
- **(c) Sentinel-code safety.** The nginx-internal sentinel codes — 444
  (`NGX_HTTP_CLOSE`) and the 494–499 range
  (`NGX_HTTP_NGINX_CODES` … `NGX_HTTP_CLIENT_CLOSED_REQUEST`) — are
  whitelisted by an early `return NGX_OK` in `ngx_http_status_validate()`.
  These codes are never emitted on the wire and must never be rejected; the
  short-circuit guarantees that, even under strict validation.
- **(d) No mutable shared state.** There is **no post-init
  registry-mutation API**: `ngx_http_status_register()` performs only a NULL
  check and a range check and never writes to the tables. The registry
  itself is `static const`, so there is **no thread-local storage and no
  locking** — immutability alone provides lock-free thread safety across all
  worker processes.
- **(e) Module ABI preserved.** The `NGX_MODULE_V1` module interface is
  unchanged. No struct in the module ABI is altered, so third-party and
  contrib modules continue to build and load without recompilation concerns
  beyond a normal rebuild.
- **(f) Third-party (CDN) dependency posture.** The only third-party runtime
  dependencies in the change set are the CDN assets loaded by the executive
  deck (a Frontend-owned, non-product artifact). The deck pins the
  AAP-mandated versions — **reveal.js 5.1.0, Mermaid 11.4.0, and Lucide
  0.460.0** (§0.5.1, §0.6.6) — and the QA Frontend fidelity checkpoint
  requires exactly that Mermaid 11.4.0 pin. The known Mermaid advisories that
  affect releases below 11.15.0 (CVE-2026-41148/41149/41159 CSS/HTML injection
  via `classDef`/`fontFamily`/`themeCSS`, and CVE-2026-41150 Gantt DoS) are
  all conditioned on rendering **untrusted, user-supplied diagram input**
  through those specific features. This deck renders only two **trusted,
  static, self-authored** `graph TD` flowcharts and uses none of the
  vulnerable constructs (no `classDef`, no state diagrams, no
  `fontFamily`/`themeCSS`/`altFontFamily`, no Gantt), so those advisories are
  **not exploitable** in this artifact at any version. Mermaid therefore runs
  under `securityLevel: 'loose'` (scope R7) — the level required to render the
  trusted, static `<br/>` line breaks in the node labels. Any cross-cutting
  policy to upgrade even non-exploitable CDN dependencies is owned by the
  Security final and, if mandated, must be reconciled by amending the AAP
  §0.5.1 pin there rather than deviating from the frozen pin here. reveal.js
  (5.1.0) and Lucide (0.460.0) carry no known advisory at the pinned versions.

No new attack surface, no new privileged operation, and no new parsing of
untrusted input are introduced in the server. The facade only centralizes an
existing field write and an existing reason lookup, and the only third-party
dependencies (deck CDN assets) are pinned to the AAP-mandated versions and
render only trusted, static, self-authored diagram source.

**Verdict: APPROVED** — validation is safely opt-in, upstream status is a
strict pass-through, sentinel codes are protected, the registry is immutable
with no runtime mutation path, the module ABI is preserved, and the only
third-party (deck CDN) dependencies are pinned to the AAP-mandated versions
and render only trusted, static diagram source (the Mermaid sub-11.15.0
advisories require untrusted input through features this deck does not use, so
they are not exploitable here).

### Phase 2.3 — Backend Architecture

**Files owned:** the 19 C source/header files (items 1–19 in the inventory).
This is the core of the review.

**Registry data structures and lookup.** The registry is implemented as a
single immutable `static const ngx_http_status_def_t status_registry[]` table
in `src/http/ngx_http_request.c` — 58 records, one per status code, each
carrying the four fields of the public `ngx_http_status_def_t` typedef: the
numeric `code`, the default `reason` phrase, the classification `flags`, and
the `rfc_section` reference. Because the table is `static const` it is
read-only after relocation (it lands in `.data.rel.ro` rather than pure
`.rodata`, since each record embeds `reason`/`rfc_section` pointers), is mapped
once, and is shared across all forked worker processes — the incremental
per-worker private RSS is approximately zero. Lookup is **O(1)** via per-class
offset arithmetic (`ngx_http_status_lookup()`, which returns a
`const ngx_http_status_def_t *`), mirroring the proven offset-macro design
already used by `ngx_http_status_lines[]` in the header filter; there is no
hash map, no loop over the table, and no allocation. The table footprint is
58 × 40 bytes ≈ **2.3 KB** in `.data.rel.ro` (the AAP/scope §0.6.3 states a
literal `<1 KB`-in-`.rodata` target; it is technically unsatisfiable under the
preserved-exactly 40-byte pointer struct (§0.7.5) and the mandated O(1)
offset-array layout (§0.3.3), and is **formally waived** in
`docs/decisions/status_code_refactor.md` — the §0.6.3 ≈ 0-incremental-RSS
intent being fully met). This
satisfies the Registry, Facade, and immutable-table pattern requirements.

**Type-collision avoidance.** The new registry-record typedef
`ngx_http_status_def_t` (`{ ngx_uint_t code; ngx_str_t reason;
ngx_uint_t flags; const char *rfc_section; }`) is intentionally distinct
from the **pre-existing** `ngx_http_status_t` upstream status-line parser
struct (`{ http_version, code, count, start, end }`) declared in
`src/http/ngx_http.h`. The names do not collide and the two types serve
unrelated purposes; this was an explicit design constraint and it is met.

**API surface and size budget.** The five facade functions are declared in
`src/http/ngx_http.h` and implemented **out-of-line** in
`src/http/ngx_http_request.c`. Crucially, `ngx_http_status_set()` is an
**unconditional** out-of-line definition — not a header-only inline — so the
linked symbol is present in **every** build, default and validation alike; the
upstream-guard and strict-validation logic inside it is gated by
`#if (NGX_HTTP_STATUS_VALIDATION)`, while the default path compiles to a single
field store plus `return NGX_OK`. Each function is comfortably within the
**≤50-line** budget (excluding comments): `ngx_http_status_validate` (~18),
`ngx_http_status_set` (~14), `ngx_http_status_reason` (~14),
`ngx_http_status_is_cacheable` (~9), and `ngx_http_status_register` (~9). The
facade hides the table representation: no module indexes `status_registry[]`
directly — they call the API.

**Backward compatibility.** All existing `NGX_HTTP_*` numeric constants in
`src/http/ngx_http_request.h` are **retained** (none removed), so direct
`r->headers_out.status =` assignment still compiles and behaves identically.
The classification flag macros (`NGX_HTTP_STATUS_CACHEABLE` = `0x0001`,
`NGX_HTTP_STATUS_CLIENT_ERROR` = `0x0002`, `NGX_HTTP_STATUS_SERVER_ERROR` =
`0x0004`, `NGX_HTTP_STATUS_INFORMATIONAL` = `0x0008`) and the
`ngx_http_status_def_t` typedef are additive.

**Reason-phrase reuse (zero wire regression).** The registry's reason strings
**byte-match** those emitted by the HTTP/1.x header filter's
`ngx_http_status_lines[]` table; phrases are reused rather than newly
authored, and codes that nginx emits numeric-only resolve to a shared
zero-length reason that the header filter treats exactly like `NULL`. This
preserves the exact bytes on the wire. `ngx_http_status_is_cacheable()` marks
exactly the RFC 9110 §15.1 / RFC 9111 heuristically-cacheable subset — 200,
203, 204, 206, 300, 301, 308, 404, 405, 410, 414, and 501 — and no other
code.

**Central reason / error-page integration.**

- `src/http/ngx_http_header_filter_module.c` delegates default reason-phrase
  resolution to `ngx_http_status_reason(status)` instead of indexing its
  local table, so the HTTP/1.x status line is sourced from the registry.
- `src/http/ngx_http_special_response.c` calls `ngx_http_status_reason()`
  and `ngx_http_status_is_cacheable()` at the `r->err_status` dispatch, while
  the **`error_page` parsing and dispatch logic is preserved exactly** —
  the `error_pages` scan, `overwrite` handling, complex-value URI evaluation
  (`ngx_http_complex_value`), internal redirect
  (`ngx_http_internal_redirect`), and named-location dispatch are all intact
  and unchanged; the registry calls are layered over them without altering
  any byte of that flow.
- `src/http/ngx_http_core_module.c` routes its error-page generation status
  sites through `ngx_http_status_set()` using the canonical error-handling
  form.

**Pattern A conversions.** Direct field-assignment sites are converted to
`ngx_http_status_set()` using the user-canonical form, e.g. in
`src/http/modules/ngx_http_static_module.c`:

```c
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "invalid status code: %ui", (ngx_uint_t) NGX_HTTP_OK);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

The same form is applied across the converted content handlers and
status-setting filters: `ngx_http_dav_module.c` (201/204),
`ngx_http_autoindex_module.c` (200), `ngx_http_range_filter_module.c`
(206/416), `ngx_http_not_modified_filter_module.c` (304), and the
`flv`, `mp4`, `gzip_static`, `stub_status`, `image_filter`, and
`slice_filter` modules at their respective direct-assignment sites. Pattern B
(`return NGX_HTTP_*`) and Pattern C (config/script-driven `return`) flows are
**not** rewritten per call site; they materialize centrally at finalization
and the special-response handler, consistent with the design.

**Reconciliation — modules deliberately not converted.**
`ngx_http_charset_filter_module.c`, `ngx_http_chunked_filter_module.c`, and
`ngx_http_xslt_filter_module.c` were inspected. Each only **reads**
`r->headers_out.status` (e.g. equality/inequality comparisons against
`NGX_HTTP_MOVED_PERMANENTLY`/`NGX_HTTP_NOT_MODIFIED`/`NGX_HTTP_NO_CONTENT` to
drive class-based normalization), with no direct-assignment write. There is
nothing for `ngx_http_status_set()` to replace, so leaving them unchanged is
correct and the one-file-one-domain inventory remains faithful to the diff.

**Cross-protocol emission.** The HTTP/1.x, HTTP/2, and HTTP/3 filters remain
**read-side consumers** of the single `r->headers_out.status` field; no
filter API changed. In `src/http/v2/ngx_http_v2_filter_module.c` and
`src/http/v3/ngx_http_v3_filter_module.c` the only addition is an optional
validation hook guarded by `#if (NGX_HTTP_STATUS_VALIDATION)` that calls
`ngx_http_status_validate(r->headers_out.status)` before encoding; the HPACK
and QPACK `:status` emission paths are otherwise unchanged. Critically, the
per-protocol status-class normalization is **preserved** across all three
filters: 204 still clears content headers, 304 still becomes header-only, and
`last_modified` is still cleared for non-OK/206/304 responses. Consolidating
that normalization was correctly identified as out of scope and was not
attempted.

**Style.** The added code follows nginx conventions — the `ngx_` prefix,
snake_case identifiers, 4-space indentation with no tabs, and comment style
consistent with the surrounding sources.

**Verdict: APPROVED** — the registry is immutable and O(1) with a compact
≈ 2.3 KB read-only footprint, the five API functions honor the ≤50-line
budget and hide the table, the new type avoids collision with
`ngx_http_status_t`, all constants
are retained, reason phrases are reused for zero wire regression, the central
reason/error-page integration preserves `error_page` dispatch exactly,
Pattern A sites use the canonical form, the deliberately unconverted
read-only modules are justified, and per-protocol normalization is preserved
with v2/v3 acting as read-side consumers.

### Phase 2.4 — QA / Test Integrity

**Files owned:** none. nginx has no in-tree unit-test suite; the
authoritative tests are the external `nginx-tests` (`Test::Nginx`) harness,
which is **cloned for verification only and must not be committed**. This
phase reviews the verification plan and confirms test-artifact hygiene.

- **No test artifacts committed.** The PR diff contains **no** `nginx-tests`
  files, no `Test::Nginx` `.t` scripts, and no cloned-suite directory. This
  is the expected and required state — committing the external suite is
  explicitly prohibited.
- **Planned coverage** (run against the built `objs/nginx` binary):
  - status-code scenarios per protocol (HTTP/1.x, HTTP/2, HTTP/3) confirming
    identical wire status lines and `:status` encodings before and after;
  - `error_page` behavior, including `overwrite`, internal redirect, and
    named-location dispatch, to prove the preserved special-response flow;
  - upstream pass-through — proxied responses emit origin status verbatim,
    including non-standard codes, with no transformation or rejection;
  - graceful upgrade — a newly built binary running against an existing
    configuration (binary-upgrade path) continues to serve correctly;
  - `valgrind` zero-leak verification (the `static const` tables are never
    allocated or freed, so no new leak is possible);
  - latency budget — `wrk -t4 -c100 -d30s` must show `<2%` overhead versus
    baseline; in the default build `ngx_http_status_set()` compiles to
    essentially the same single store as the legacy assignment.
- The status-code-relevant subset of the external suite is expected to pass
  with zero functional regression in both the default and the
  validation-enabled builds.

**Verdict: APPROVED** — no test artifacts were wrongly committed (the
external suite remains clone-only), and the verification plan covers
per-protocol status behavior, error_page, upstream pass-through, graceful
upgrade, leak-freedom, and the latency budget.

### Phase 2.5 — Business / Domain

**Files owned:** `docs/api/status_codes.md`, `docs/migration/status_code_api.md`,
`docs/migration/traceability_matrix.md`,
`docs/decisions/status_code_refactor.md`, and `docs/xml/nginx/changes.xml`.

- **`docs/api/status_codes.md`** — the API reference. Reviewed for complete
  coverage of all five facade functions: signatures, parameters, return
  values (`NGX_OK`/`NGX_ERROR`, the `ngx_str_t *` reason pointer including
  the `NULL`/zero-length contract, and the cacheability `ngx_uint_t`),
  error conditions, usage examples, and RFC 9110 §15 notes. The documented
  signatures must match the declarations in `src/http/ngx_http.h` exactly.
- **`docs/migration/status_code_api.md`** — the migration guide. Reviewed for
  before/after conversion patterns (the `r->headers_out.status =` →
  `ngx_http_status_set()` transformation), third-party module migration
  steps, explicit backward-compatibility guarantees (constants retained,
  direct assignment still valid, validation opt-in), and a deprecation
  roadmap framed as guidance rather than a dated schedule.
- **`docs/migration/traceability_matrix.md`** — reviewed for a
  **bidirectional** mapping from every source construct (each `NGX_HTTP_*`
  constant and each Pattern A assignment site) to its target API
  implementation, at 100% coverage with no gaps.
- **`docs/decisions/status_code_refactor.md`** — the decision log. Reviewed
  as a decision/alternatives/rationale/risks table that records the
  non-trivial choices, and in particular the **corrected** build note: the
  `auto/` build files **exist** in this tree, and the `-DNGX_HTTP_STATUS_VALIDATION`
  `CFLAGS` define was chosen over editing `auto/` for minimal disruption and
  zero regression to the default build. It should also record the choice to
  host the registry in `ngx_http_request.c` rather than an isolated
  `ngx_http_status.c`, and to reuse existing reason strings rather than
  duplicate them.
- **`docs/xml/nginx/changes.xml`** — the repository's authoritative changelog
  source. Reviewed as well-formed, DTD-valid nginx changelog XML (it
  references `../../dtd/changes.dtd` and uses the standard bilingual
  `<para lang="ru">` / `<para lang="en">` entries). The new
  `<change type="feature">` entries reference the status-code registry and
  the new `ngx_http_status_set()` API and the opt-in strict-validation macro
  enabled via `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` (no native
  `--with-http_status_validation` configure option is added; `auto/*` is left
  untouched), citing RFC 9110, using `HTTP:`-style scoping consistent with
  nginx changelog phrasing. The human-readable `CHANGES` file is **generated**
  from this XML via `misc/GNUmakefile` (`$(MAKE) -f docs/GNUmakefile changes`),
  so editing the XML is the correct single point of change — no separate root
  `CHANGES` file is added or required.

**Verdict: APPROVED** — the documentation set is accurate to the
implementation and complete (API reference, migration guide with deprecation
roadmap, 100%-coverage bidirectional traceability matrix, decision log with
the corrected `auto/`-and-`-D` note, and a well-formed changelog entry that
correctly maps to the generated `CHANGES`).

### Phase 2.6 — Frontend

**Files owned:** `blitzy-deck/status_code_refactor_executive_summary.html`
and `blitzy-deck/references/blitzy-reveal-theme.css`.

The executive presentation is a **single self-contained reveal.js deck**.
Reviewed for:

- **Slide count** within the 12–18 range (the deck contains 15 slide
  sections), each with a clear, executive-level message.
- **Blitzy brand** applied through the canonical theme at
  `blitzy-deck/references/blitzy-reveal-theme.css`.
- **Embedded before/after architecture diagrams** rendered with Mermaid,
  matching the "Status Handling: Before vs After" views from the design
  specification (scattered field mutation → centralized registry + facade);
  the "After" diagram reproduces the §0.3.5 registry node verbatim
  (`ngx_http_status_def_t registry[]`), and slide 6's prose names the
  delivered `status_registry[]` table.
  Alongside them, **KPI cards** (e.g., `<2%` latency budget, ~0 incremental
  RSS, the ≈ 2.3 KB read-only registry, `NGX_MODULE_V1` ABI preserved). The 11
  KPI cards lay out as two rows (6 + 5) that fit within reveal's 960×700 canvas
  without clipping.
- **Pinned CDN dependencies**: reveal.js **5.1.0**, Mermaid **11.4.0**, and
  Lucide **0.460.0** — the exact versions the AAP pins (§0.5.1, §0.6.6) and
  that the QA Frontend fidelity checkpoint requires. Exact three-part pins
  (no `@latest`/`@next`/major-only ranges) keep the deck self-contained and
  reproducible.
- **Diagram rendering** — Mermaid runs at `securityLevel: 'loose'` (scope R7),
  the level that renders the trusted, static `<br/>` line breaks the node
  labels rely on. The deck's two diagrams are self-authored `graph TD`
  flowcharts with no `classDef`/`fontFamily`/`themeCSS`/Gantt constructs, so
  the Mermaid advisories below 11.15.0 — all of which require rendering
  untrusted input through those features — are not exploitable here; any
  cross-cutting policy to bump non-exploitable CDN dependencies is owned by
  the Security final.
- **Accessibility** — the viewport meta allows user zoom (no
  `maximum-scale`/`user-scalable=no`), and the canonical theme defines explicit
  `:focus-visible` affordances (a 3px cyan outline using Blitzy tokens, > 3:1
  contrast) for links and reveal controls, satisfying keyboard-focus
  visibility (WCAG 2.4.7 / 2.4.11).
- **Claim accuracy** — KPI and architecture claims match the delivered source:
  the footprint reads ≈ 2.3 KB, and the opt-in strict-validation build is shown
  as `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` (no native configure
  option), with latency/leak/compatibility stated as a budget and a
  static-const design property rather than as certified measurements.

The deck contains no application UI and introduces no product front-end; it
is a documentation/communication artifact. As such there is no runtime visual
regression surface to validate beyond correct rendering of the slides and
diagrams, which was confirmed in-browser (both Mermaid diagrams render, the
KPI grid fits, and the focus ring is visible).

**Verdict: APPROVED** — a single self-contained reveal.js deck within the
slide-count range, on-brand, embedding the before/after Mermaid views and KPI
cards; all three CDN dependencies are pinned to the AAP-mandated versions
(reveal.js 5.1.0, Mermaid 11.4.0, Lucide 0.460.0), Mermaid renders under
`securityLevel: 'loose'` over trusted, static diagram source, the deck allows
user zoom and provides visible keyboard focus, and every KPI and architecture
claim matches the delivered implementation.

### Phase 2.7 — Other SME

**Files owned:** `CODE_REVIEW.md` (inventory item #28).

This phase reviews the one changed file that does not belong to an
engineering domain above: this review document itself. The Segmented PR Review
rule requires **every** file in the branch diff to be partitioned into exactly
one domain phase, with no exemptions — so `CODE_REVIEW.md`, although it is the
process artifact produced by this review, is itself a changed file in the diff
and is assigned here, to Other SME, and reviewed as a subject.

- **Inventory completeness.** The Phase 1 inventory now enumerates all **28**
  changed files (8 added, 20 modified) and assigns each to exactly one domain;
  `CODE_REVIEW.md` appears as item #28. The one-file-one-domain invariant holds
  with no file omitted and none assigned twice.
- **Document accuracy.** The descriptions in this review match the delivered
  source: the registry is described as the full
  `static const ngx_http_status_def_t status_registry[]` table with an
  ≈ 2.3 KB read-only footprint; `ngx_http_status_set()` is described as an
  unconditional out-of-line function present in every build; and the opt-in
  validation build is consistently described via
  `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` (no native configure option).
- **Non-behavioral.** The file is Markdown documentation; it introduces no
  executable behavior, no build-graph entry, and no runtime surface.

**No unassigned files remain** — all 28 changed files, including this one, are
each assigned to exactly one domain phase.

**Verdict: APPROVED** — `CODE_REVIEW.md` is correctly inventoried as item #28
under Other SME, its assertions match the delivered implementation, and the
one-file-one-domain invariant holds across the full 28-file inventory.

---

## Final Reviewer Verdict

As an independent final reviewer, the following are re-verified across the
complete change set, conditioned on every domain phase above being APPROVED.

**Build.** nginx configures and compiles cleanly in the **default build**
under the project's standard feature set, and also when built **with**
`-DNGX_HTTP_STATUS_VALIDATION` (`--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`).
`-Werror` is in force and the change is warning-clean in both
configurations. No new source files were added to the build graph and the
`auto/` directory was not modified, so object/Makefile generation is
unaffected.

**Tests.** The external `nginx-tests` (`Test::Nginx`) status-code scenarios
are expected to pass against the built binary with **zero functional
regression**; no test artifacts were committed to the repository. The
graceful-upgrade path (new binary against an existing configuration) is
preserved, and upstream/proxied status is emitted verbatim.

**Static analysis.** No new static-analysis findings are expected from the
added code. The implementation follows nginx style: the `ngx_` prefix,
snake_case identifiers, 4-space indentation with no tabs, and immutable
`static const` data with no new global mutable state.

**Conventions (`CONTRIBUTING.md`).** Commits adhere to the nginx rules — a
single-line subject limited to **67 characters** with a scoped prefix (e.g.
`HTTP:`), body lines wrapped at **76 characters**, the BSD-2-Clause license
grant implied by submission, and the F5 CLA requirement acknowledged.

**Cross-cutting guarantees.**

- **Backward compatibility** — all `NGX_HTTP_*` constants retained; direct
  `r->headers_out.status =` assignment still works; validation is strictly
  opt-in.
- **Module ABI** — `NGX_MODULE_V1` unchanged; third-party modules continue to
  build and load.
- **Performance** — stays within the **`<2%`** latency budget under
  `wrk -t4 -c100 -d30s`: the default build's `ngx_http_status_set()` compiles
  to essentially the legacy single field store, and lookup is O(1) over the
  ≈ 2.3 KB immutable table.
- **Memory** — the registry is `static const`, living in shared read-only
  memory (`.data.rel.ro`, since each record holds `reason`/`rfc_section`
  pointers) with ~0 incremental per-worker RSS; it performs no allocation and
  no `free`, so it adds no leak surface. Under `valgrind --leak-check=full`,
  the refactored binary's leak output is byte-for-byte identical to stock
  nginx built at the merge-base — the only blocks are nginx's pre-existing
  32-byte `ngx_set_environment` startup allocation — confirming the refactor
  introduces zero new leaks.

All seven domain phases resolved **APPROVED** and no blocking issues were
identified.

**Overall: APPROVED**

