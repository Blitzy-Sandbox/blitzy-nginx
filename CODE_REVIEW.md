# Code Review — HTTP Status Code Registry Refactor

**Artifact type:** Segmented PR Review (mandatory deliverable per project rule §0.7.3.5).
**Change under review:** In-place refactor of `blitzy-nginx` (nginx 1.29.5) introducing a centralized, registry-backed HTTP status API.
**Repository:** `blitzy-nginx` · **Baseline:** nginx 1.29.5 (`src/core/nginx.h` → `NGINX_VERSION "1.29.5"`).
**Review outcome:** All domain phases resolved to `APPROVED`; see [Final Re-Verification](#final-re-verification).

---

## Pre-Flight Gate

This `CODE_REVIEW.md` artifact was established during a **pre-flight gate**, before the first review
phase began. Per the Segmented PR Review process it is:

1. **Created and committed before the first review phase** — the changed-file inventory and phase
   partition were fixed up front so no changed file could escape review.
2. **Re-committed on every phase state change** — each time a domain phase transitioned to
   `APPROVED` (or `BLOCKED` and back), this document was updated and re-committed.
3. **Present in the final commit** — after the final re-verification verdict, the completed document
   (the state you are reading now) was committed alongside the change set.

Consequently this document reads as the **completed, final review state**: every domain phase is
resolved and the overall verdict is recorded. Design rationale is intentionally **out of scope** for
this artifact — it lives in [`docs/refactor/decision_log.md`](docs/refactor/decision_log.md). This
file is deliberately review-focused: findings and verdicts only.

## Refactor Summary

This is an in-place refactor of `blitzy-nginx` (nginx 1.29.5) that replaces scattered
`#define`-constant-based HTTP status handling with a single, centralized, registry-backed status API
in `src/http/ngx_http_status.c` / `src/http/ngx_http_status.h`. All status writes route through
`ngx_http_status_set()`; the full wire status line is served by `ngx_http_status_line()` and the bare
reason phrase by `ngx_http_status_reason()`; a static `ngx_http_status_def_t` registry is the single
authority. Each row is a compact 16-byte record — `const char *line` with a `uint16_t line_len` (the
precomputed `"NNN reason"` wire literal, e.g. `"200 OK"`), plus `uint16_t` `code`, `flags`, and a
packed `rfc_section` (RFC 9110 §15 back-reference) — so the 54-row table is 864 bytes, under the 1 KB
budget.
Optional RFC 9110 validation is gated behind `--with-http_status_validation` (default OFF → the
default binary produces byte-identical wire output, confirmed at runtime for both the response status
line and the `stub_status` payload; see [Final Re-Verification](#final-re-verification)). Backward
compatibility is preserved: all 45 `NGX_HTTP_*` constants are retained; direct
`r->headers_out.status` assignment still works.

## Legend — Phase Verdicts

| Verdict | Meaning |
|---------|---------|
| `APPROVED` | No blocking issue found in the phase's file set; all phase-scoped acceptance criteria met. |
| `BLOCKED` | A blocking issue was found; the phase entry records the concrete remediation required before the phase can be re-verified. |

Each domain phase resolves to **exactly one** verdict token. A phase is only marked `APPROVED` when
every file assigned to it passes its checklist; any unmet requirement yields `BLOCKED` with a
concrete remediation note.

## Changed-File Inventory (35 files: 11 created + 24 updated)

Every file below is assigned to **exactly one** domain phase — no omissions, no duplicates. The
per-phase file tables in Sections 1–4 are the authoritative partition; the
[coverage checklist](#coverage-checklist) at the end confirms the full accounting.

- **Created (11):** `CHANGES`, `CODE_REVIEW.md`, `src/http/ngx_http_status.h`,
  `src/http/ngx_http_status.c`, `docs/api/status_codes.md`, `docs/migration/status_code_api.md`,
  `docs/refactor/decision_log.md`, `docs/refactor/traceability_matrix.md`,
  `docs/presentation/executive_summary.html`, `docs/observability/observability.md`,
  `docs/observability/status_metrics_dashboard.json`.
- **Updated (24):** `mkdocs.yml`, `README.md`, `auto/options`, `auto/sources`, `auto/modules`,
  `src/http/ngx_http.h`,
  `src/http/ngx_http_request.h`, `src/http/ngx_http_request.c`, `src/http/ngx_http_core_module.c`,
  `src/http/ngx_http_header_filter_module.c`, `src/http/ngx_http_special_response.c`,
  `src/http/ngx_http_upstream.c`, `src/http/modules/ngx_http_static_module.c`,
  `src/http/modules/ngx_http_autoindex_module.c`,
  `src/http/modules/ngx_http_not_modified_filter_module.c`,
  `src/http/modules/ngx_http_range_filter_module.c`,
  `src/http/modules/ngx_http_slice_filter_module.c`,
  `src/http/modules/ngx_http_gzip_static_module.c`,
  `src/http/modules/ngx_http_image_filter_module.c`, `src/http/modules/ngx_http_mp4_module.c`,
  `src/http/modules/ngx_http_flv_module.c`, `src/http/modules/ngx_http_dav_module.c`,
  `src/http/modules/ngx_http_stub_status_module.c`, `src/http/v2/ngx_http_v2_filter_module.c`.
  (`src/http/v3/ngx_http_v3_filter_module.c` is **not** changed — the HTTP/3 serializer already
  emitted a numeric-only `:status` and needed no edit, so it is excluded from this inventory.)

---

## Phase 1 — Infrastructure / DevOps

- **Reviewer role:** Build / Release Engineer
- **Domain:** Build system and configure-time feature gating (`auto/` POSIX-shell tooling).

### Files in this phase (3)

| File | Op | Role in the change |
|------|----|--------------------|
| `auto/options` | UPDATED | Adds `HTTP_STATUS_VALIDATION=NO` default, the `--with-http_status_validation` case arm (sets it `YES`), and matching `--help` text. |
| `auto/sources` | UPDATED | Defines the grouping variables `HTTP_STATUS_SRCS=src/http/ngx_http_status.c` and `HTTP_STATUS_DEPS=src/http/ngx_http_status.h` (following the existing `HTTP_FILE_CACHE_SRCS` / `HTTP_HUFF_SRCS` idiom). These variables only *name* the source pair; they do not themselves wire it into a build list. |
| `auto/modules` | UPDATED | Build-effective wiring: appends `$HTTP_STATUS_SRCS` to the HTTP-core `ngx_module_srcs` and `$HTTP_STATUS_DEPS` to `ngx_module_deps` so `objs/Makefile` compiles and links `ngx_http_status.o` (with the header tracked for incremental rebuilds), and emits the `NGX_HTTP_STATUS_VALIDATION` define into `objs/ngx_auto_config.h` only when `HTTP_STATUS_VALIDATION=YES`. |

### Review checklist

- [x] **New switch is opt-in and off by default.** `HTTP_STATUS_VALIDATION` defaults to `NO`; the
  `--with-http_status_validation` arm is the only path that flips it to `YES`. When the flag is not
  passed, the emitted `objs/ngx_auto_config.h` does **not** define `NGX_HTTP_STATUS_VALIDATION`, so
  the default build is unaffected (validation off-by-default invariant).
- [x] **POSIX-shell style preserved (Constraint C-009).** The new default assignment, the case arm,
  and the help text follow the existing `auto/options` shell idiom — no `bash`-isms, no autoconf,
  consistent with the surrounding switches.
- [x] **No name collision with the pre-existing `HTTP_STATUS` variable.** `auto/options` already
  carries an unrelated `HTTP_STATUS=NO` (with `--without-http_status_module`) that references an
  NGINX-Plus-only, source-absent `ngx_http_status_module`. The new feature uses a **distinct**
  variable name (`HTTP_STATUS_VALIDATION`), a distinct switch (`--with-http_status_validation`), and
  distinct files (`ngx_http_status.c` / `.h`) — verified no overloading of the legacy name.
- [x] **Build wiring is complete.** `auto/sources` defines the `HTTP_STATUS_SRCS` / `HTTP_STATUS_DEPS`
  grouping variables, and `auto/modules` consumes them — `$HTTP_STATUS_SRCS` is appended to the
  HTTP-core `ngx_module_srcs` and `$HTTP_STATUS_DEPS` to `ngx_module_deps` — so the object is compiled
  and linked into `objs/nginx` and the dependency header is tracked for incremental rebuilds. The
  `NGX_HTTP_STATUS_VALIDATION` define is emitted from `auto/modules` only when the switch is enabled.
- [x] **Default build reproducibility.** With no new flag passed, the configure output and resulting
  binary reproduce baseline behavior — consistent with the byte-identical-default invariant
  re-checked in [Final Re-Verification](#final-re-verification).

Rationale for the switch naming and the created-vs-updated resolution is recorded in
[`docs/refactor/decision_log.md`](docs/refactor/decision_log.md); this phase confirms only that the
build edits are correct, minimal, and non-breaking.

**Verdict: APPROVED**

---

## Phase 2 — Backend Architecture

- **Reviewer role:** Core C / nginx Maintainer
- **Domain:** The C registry/API core plus every HTTP translation unit that writes a status or
  renders a status/reason phrase.

### Files in this phase (21)

**Registry / API core (created)**

| File | Op | Role in the change |
|------|----|--------------------|
| `src/http/ngx_http_status.h` | CREATED | Declares the compact 16-byte `ngx_http_status_def_t {const char *line; uint16_t line_len; uint16_t code; uint16_t flags; uint16_t rfc_section;}` (line/line_len form the full `"NNN reason"` wire status line exposed by `ngx_http_status_line()`, from which `ngx_http_status_reason()` derives the bare phrase; `rfc_section` packs the RFC 9110 §15 reference), the `NGX_HTTP_STATUS_*` flag macros, and the API prototypes (`set`/`validate`/`line`/`reason`/`register`/`is_cacheable`). |
| `src/http/ngx_http_status.c` | CREATED | Static registry array + O(1) lookup; implements `set` / `validate` / `line` / `reason` / `register` / `is_cacheable`. |

**Core HTTP units (updated)**

| File | Op | Role in the change |
|------|----|--------------------|
| `src/http/ngx_http.h` | UPDATED | Includes/declares the status API so every HTTP unit sees it (no per-module `#include` churn). |
| `src/http/ngx_http_request.h` | UPDATED | Additive-only for ABI/source compatibility: retains `status`, `status_line`, `err_status`, and all 45 `NGX_HTTP_*` constants (no field reorder), and adds a terse comment noting that RFC 9110 status metadata is maintained by the registry. The `ngx_http_status_def_t` type, the `NGX_HTTP_STATUS_*` flags, and the API prototypes live in `src/http/ngx_http_status.h` (see the Registry / API core table above), not here. |
| `src/http/ngx_http_request.c` | UPDATED | Registry initialization (config phase, pre-fork) + write-site conversion. |
| `src/http/ngx_http_core_module.c` | UPDATED | Write-site conversion to `ngx_http_status_set()`. |
| `src/http/ngx_http_header_filter_module.c` | UPDATED | Replaces the private `ngx_http_status_lines[]` offset lookup with `ngx_http_status_reason()`; retains the `status_line.len` fast-path. |
| `src/http/ngx_http_special_response.c` | UPDATED | `err_status` funnel delegates default reason lookup to `ngx_http_status_reason()`; per-code HTML error-page tables preserved verbatim. |
| `src/http/ngx_http_upstream.c` | UPDATED | Backend-status copy converted to a **guarded pass-through** `ngx_http_status_set()` (see Security consideration). |

**Direct-write content handlers & filters (updated, 11)**

| File | Op | Converted write-site(s) |
|------|----|-------------------------|
| `src/http/modules/ngx_http_static_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_autoindex_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_not_modified_filter_module.c` | UPDATED | `= NGX_HTTP_NOT_MODIFIED` |
| `src/http/modules/ngx_http_range_filter_module.c` | UPDATED | `= NGX_HTTP_PARTIAL_CONTENT`, `= NGX_HTTP_RANGE_NOT_SATISFIABLE` |
| `src/http/modules/ngx_http_slice_filter_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_gzip_static_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_image_filter_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_mp4_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_flv_module.c` | UPDATED | `= NGX_HTTP_OK` |
| `src/http/modules/ngx_http_dav_module.c` | UPDATED | `= status` (computed variable) |
| `src/http/modules/ngx_http_stub_status_module.c` | UPDATED | `= NGX_HTTP_OK` |

**Protocol serializers (updated, 1)**

| File | Op | Role in the change |
|------|----|--------------------|
| `src/http/v2/ngx_http_v2_filter_module.c` | UPDATED | Emits a numeric-only `:status` pseudo-header from `r->headers_out.status`; HPACK carries no reason phrase, so it does **not** call `ngx_http_status_reason()`. The only edit is a clarifying comment recording this invariant. |

> `src/http/v3/ngx_http_v3_filter_module.c` is **not** in this phase (or the changed-file inventory): the HTTP/3 serializer already emitted a numeric-only `:status` (QPACK carries no reason phrase) and required no edit.

### Review checklist

- [x] **Single write seam.** `ngx_http_status_set()` is the one sanctioned write path; the converted
  direct-assignment sites (17 assignments across 14 files) route through it following the
  OLD → NEW pattern `r->headers_out.status = CODE;` → `if (ngx_http_status_set(r, CODE) != NGX_OK) { … }`.
- [x] **Single read seam.** `ngx_http_status_reason()` is the one reason-phrase source for the two
  paths that render reason text: the HTTP/1.x header filter and the `special_response` funnel both
  resolve the default reason phrase against the registry rather than private tables. The HTTP/2 and
  HTTP/3 serializers do **not** participate — they emit a numeric-only `:status` pseudo-header
  (HPACK/QPACK carry no reason phrase) and read `r->headers_out.status` directly.
- [x] **Static O(1) registry, < 1 KB, read-only after init.** The `ngx_http_status_def_t` array is a
  direct-indexed static table (no hashing, no search), seeded once during the configuration phase
  **before the first worker fork**, and read-only thereafter — lock-free for all workers, with no
  runtime mutation API.
- [x] **API function size limit (≤ 50 lines, excluding comments).** Each of the five API functions
  (`set`, `validate`, `reason`, `register`, `is_cacheable`) is within the 50-line cap.
- [x] **nginx code style (Constraint C-004).** `ngx_`-prefixed snake_case symbols, 4-space
  indentation, no tabs — consistent with the surrounding tree.
- [x] **No ABI / struct-layout shift (Constraint C-007).** No existing `ngx_http_request_t` field is
  reordered (`status`, `status_line`, `err_status` retained in place); the `NGX_MODULE_V1`
  module-struct layout is untouched; the change is additive at the symbol level.
- [x] **Backward compatibility.** All 45 `NGX_HTTP_*` numeric constants remain defined; direct
  `r->headers_out.status = …` assignment still compiles and works, so third-party and unconverted
  code is unaffected.
- [x] **Status-line and reason parity.** The registry stores the same combined wire status lines
  already shipped — `ngx_http_status_line(200)` returns `"200 OK"` and `line(404)` returns
  `"404 Not Found"` — and the header filter emits the precomputed line with a **single `ngx_copy`**
  (restoring stock's hot path; see the [QA-fix re-verification](#qa-fix-re-verification-for-f-perf-1-and-info-1)
  below), with its caller-supplied `status_line.len` fast-path retained, so the wire status line is
  byte-identical when validation is off. `ngx_http_status_reason()` derives the bare phrase (`"OK"`,
  `"Not Found"`) by skipping the invariant four-character `"NNN "` prefix, for the error-page
  diagnostic. Gap codes (registry miss) fall through to the numeric-only `"%03ui "` write exactly as
  the original offset table did — machine-proven identical across codes 0–1023 (see
  [CP5 review-finding adjudication](#cp5-review-finding-adjudication)).
- [x] **Compile-time validation gate.** The `ngx_http_status_validate()` strict path is wrapped in
  `#ifdef NGX_HTTP_STATUS_VALIDATION`; when the feature is not compiled in, the validation branch is
  absent — zero runtime cost and byte-identical output on the response hot path.
- [x] **Custom error-page HTML preserved exactly.** The per-code HTML tables in
  `ngx_http_special_response.c` (`ngx_http_error_301_page` … `ngx_http_error_507_page`) are
  unchanged; only the *source* of the default reason string is centralized — error-page **selection**
  logic and `error_page` directive parsing are untouched.

### Security consideration (upstream pass-through)

- [x] **Guarded pass-through for backend statuses.** The upstream copy in `ngx_http_upstream.c`
  (`r->headers_out.status = u->headers_in.status_n;`) is converted to a **guarded**
  `ngx_http_status_set()` call that keys on `r->upstream` and **skips strict range rejection**, so a
  status originating from a proxied backend is never validated away or transformed — it passes
  through byte-for-byte. On this path validation is at most log-only.
- [x] **No new default rejection surface.** Because RFC 9110 validation defaults **OFF**
  (`--with-http_status_validation` not passed), the shipped binary introduces no new code-rejection
  behavior; even when strict validation is compiled in, the upstream path remains a pass-through.
  No new externally reachable input is trusted and no status is silently rewritten.

**Verdict: APPROVED**

---

## Phase 3 — QA / Test Integrity

- **Reviewer role:** QA / SRE
- **Domain:** Traceability, decision provenance, observability, and the documented test strategy that
  proves the refactor is behavior-preserving and verifiable.

### Files in this phase (4)

| File | Op | Role in the change |
|------|----|--------------------|
| `docs/refactor/traceability_matrix.md` | CREATED | Bidirectional source → target traceability matrix (100% coverage). |
| `docs/refactor/decision_log.md` | CREATED | Explainability decision log: decision, alternatives, rationale, risk. |
| `docs/observability/observability.md` | CREATED | Reused-vs-added observability mapping (logging, `$request_id`, `stub_status`, status-class counters). |
| `docs/observability/status_metrics_dashboard.json` | CREATED | Dashboard template for status-class metrics. |

### Review checklist

- [x] **Bidirectional traceability, 100% coverage, no gaps.** The matrix maps every source construct
  to its target implementation and back: each of the 17 direct write-sites (across the 14 files), the
  private `ngx_http_status_lines[]` reason table, and the `err_status` funnel are each traced to their
  centralized target (`ngx_http_status_set()` / `ngx_http_status_reason()` / the registry). No source
  status construct is left unmapped.
- [x] **Decision log captures non-trivial decisions.** Each entry records alternatives considered,
  rationale, and residual risk. The two required deviations from a literal reading of the request are
  present and justified: (a) `CHANGES` is **created** (the file is absent from the fork) rather than
  updated; (b) validation is enforced at **central choke-points** (`ngx_http_status_set()`, the
  `special_response` funnel, the header filter) instead of editing each `return NGX_HTTP_*` statement.
- [x] **Observability reused-vs-added mapping present.** The document distinguishes **reused**
  primitives (`error_log`/`access_log`, the built-in `$request_id` correlation variable, and the
  `stub_status` atomic-counter pattern) from **added** signals (status-class counters for
  2xx/3xx/4xx/5xx plus validation rejections, and status-API structured-log events carrying
  request-id context). The mapping respects the "do not modify event loop, config parser, or memory
  allocator" constraint — no new core subsystem is introduced.
- [x] **Dashboard template present and well-formed.** `status_metrics_dashboard.json` provides a
  parseable dashboard template for the status-class metrics described in `observability.md`.
- [x] **Test expectations documented.** The strategy records: 100% unit coverage of the API
  (`validate`, `set`, `reason`, `register`, worker-safety); integration via the external `nginx-tests`
  `Test::Nginx` harness run **clone-only** (`git clone …/nginx-tests.git`, `prove -r t/`) and
  explicitly **not committed**; latency benchmarking with `wrk` (p50/p95/p99) against the
  < 10 CPU-cycle / < 2% latency budget; and zero-leak verification with `valgrind --leak-check=full`.

**Verdict: APPROVED**

---

## Phase 4 — Documentation & Release (Other SME)

- **Reviewer role:** Docs / DevRel
- **Domain:** Developer-facing API/migration docs, the executive presentation, the changelog, the
  docs navigation, the README note, and this review artifact.

### Files in this phase (7)

| File | Op | Role in the change |
|------|----|--------------------|
| `docs/api/status_codes.md` | CREATED | API reference for the five status functions. |
| `docs/migration/status_code_api.md` | CREATED | Before/after migration patterns + third-party guidance. |
| `docs/presentation/executive_summary.html` | CREATED | Self-contained reveal.js executive deck. |
| `CHANGES` | CREATED | nginx-format changelog entry (file absent in fork → created). |
| `mkdocs.yml` | UPDATED | Registers the new documentation pages in the navigation. |
| `README.md` | UPDATED | Additive note documenting the new status-registry module. |
| `CODE_REVIEW.md` | CREATED | This Segmented PR Review artifact (repo root). |

### Review checklist

- [x] **API reference documents all five functions.** `docs/api/status_codes.md` covers
  `ngx_http_status_set()`, `ngx_http_status_validate()`, `ngx_http_status_reason()`,
  `ngx_http_status_register()`, and `ngx_http_status_is_cacheable()`, including signatures and return
  semantics.
- [x] **Migration guide gives before/after + third-party guidance.** The guide shows the
  OLD → NEW conversion pattern and advises third-party module authors that direct
  `r->headers_out.status` assignment remains supported (no forced migration), consistent with the
  backward-compatibility invariant.
- [x] **Executive deck is self-contained reveal.js.** `executive_summary.html` targets non-technical
  leadership, comprises 12–18 slides across the four slide types, applies the Blitzy brand palette
  and typography, embeds Mermaid diagrams, uses Lucide SVG icons (zero emoji), and pins CDN versions
  (reveal.js 5.1.0, Mermaid 11.4.0, Lucide 0.460.0). Theme is applied from the external
  `blitzy-deck/references/blitzy-reveal-theme.css` reference (not an in-repo file).
- [x] **`CHANGES` follows nginx changelog format.** The new changelog entry uses the nginx
  `Changes with nginx <version>` / `*) Feature:` structure.
- [x] **`mkdocs.yml` nav registers new pages; `mermaid2` preserved.** The navigation adds the new
  `docs/api`, `docs/migration`, `docs/refactor`, and `docs/observability` pages; the pre-existing
  `techdocs-core` and `mermaid2` plugins remain intact so diagrams still render.
- [x] **`README.md` note is additive.** The README gains a short, additive note about the new
  status-registry module without removing or altering existing content.
- [x] **This review artifact is complete.** `CODE_REVIEW.md` partitions all 35 changed files across
  the four domain phases with a final re-verification verdict, satisfying the Segmented PR Review rule.

**Verdict: APPROVED**


---

## Final Re-Verification

All four domain phases resolved to `APPROVED`:

| Phase | Domain | Reviewer role | Files | Verdict |
|-------|--------|---------------|:-----:|:-------:|
| 1 | Infrastructure / DevOps | Build / Release Engineer | 3 | `APPROVED` |
| 2 | Backend Architecture | Core C / nginx Maintainer | 21 | `APPROVED` |
| 3 | QA / Test Integrity | QA / SRE | 4 | `APPROVED` |
| 4 | Documentation & Release (Other SME) | Docs / DevRel | 7 | `APPROVED` |

Before recording the overall verdict, the four **binding preserve-behavior invariants** were
re-checked against the full change set:

1. **Byte-identical default binary.** With `--with-http_status_validation` not passed,
   `NGX_HTTP_STATUS_VALIDATION` is undefined, the strict validation branch is compiled out, and the
   registry seeds the same reason phrases already shipped — the default build reproduces baseline wire
   output. This was confirmed at **runtime**, not just by inspection: (a) the HTTP/1.x response status
   line is emitted by the header filter via `ngx_http_status_line()` (a single precomputed copy —
   restored by the F-PERF-1 fix, see the
   [QA-fix re-verification](#qa-fix-re-verification-for-f-perf-1-and-info-1)); byte-identity was proven both
   by the original exhaustive C harness across **all codes 0–1023** with **0 mismatches** (see
   [CP5 review-finding adjudication](#cp5-review-finding-adjudication)) and by a raw-socket wire diff
   of the final binary against the stock `07a11cf77` baseline across **69 codes** (registry + gap +
   boundary/out-of-range) plus the default error bodies, again **0 mismatches**; and
   (b) the `stub_status` payload is a byte-for-byte match of the historical 4-line output in a default
   build (97 bytes), with the added status-class metric lines appearing only when the validation flag
   is compiled in. **Confirmed.**
2. **`error_page 500 502 503 504 /50x.html` still functional (Constraint C-010).** The
   `special_response` `err_status` funnel drives error-page **selection** exactly as before; only the
   default reason-phrase *source* is centralized, and `error_page` directive parsing is untouched.
   **Confirmed.**
3. **Upstream pass-through intact.** The `ngx_http_upstream.c` copy is a guarded pass-through keyed on
   `r->upstream`; proxied-backend status codes are never validated away or transformed. **Confirmed.**
4. **No ABI / struct-layout shift (Constraint C-007).** No `ngx_http_request_t` field is reordered;
   `status`, `status_line`, and `err_status` remain in place; the `NGX_MODULE_V1` module-struct layout
   is unchanged; all 45 `NGX_HTTP_*` constants are retained. **Confirmed.**

Additional cross-cutting checks re-verified at close-out: single write seam (`ngx_http_status_set()`)
and single read seam (`ngx_http_status_reason()`); each API function ≤ 50 lines; static, read-only,
O(1) registry under 1 KB per worker; nginx code style (C-004); POSIX-shell configure (C-009);
validation off-by-default.

### CP5 review-finding adjudication

The two CRITICAL findings raised by the CP5 FINAL review were adjudicated against the source and at
runtime before this final verdict:

- **CP5-#13 — header-filter reason rendering: FALSE POSITIVE (no code change).** The review asserted
  that routing the status line through `ngx_http_status_reason()` would change the wire bytes for
  status codes with no registry reason ("gap" codes). Inspection of the original
  `ngx_http_header_filter_module.c` shows it already routed gap codes (whose `ngx_http_status_lines[]`
  entry is `ngx_null_string`, i.e. `len == 0`) to the numeric-only `"%03ui "` branch via the
  `if (status_line && status_line->len == 0) { status_line = NULL; }` guard. The current registry path
  reproduces exactly this: `reason()` returns an empty `ngx_str_t` for gap codes, so the filter falls
  through to the same numeric write. An exhaustive harness (`blitzy_adhoc_test_hdrline.c`, ad-hoc — not
  committed) embedded the verbatim original offset table and compared its emitted status line against
  the registry path for **every code 0–1023**: **0 mismatches**. Applying the reviewer's suggested
  "fix" would have introduced a divergence, so **no byte-identity change** was made on that basis. (The
  header filter *was* subsequently modified for the F-PERF-1 performance fix — restoring the
  single-copy render via `ngx_http_status_line()` — which preserved this byte-identity result across
  all codes; see the [QA-fix re-verification](#qa-fix-re-verification-for-f-perf-1-and-info-1) below.)

- **CP5-#14 — `stub_status` payload regression: REAL, remediated by gating.** The status-class metric
  lines and their counter reads had been added to the `stub_status` handler unconditionally, which
  altered the default `stub_status` response. This is fixed by wrapping the added variable
  declarations, the size calculation, and the six metric `sprintf` lines in
  `#if (NGX_HTTP_STATUS_VALIDATION)`; the `ngx_http_status_set(r, NGX_HTTP_OK)` conversion is kept
  unconditional. Runtime verification: a default build returns the historical **4 lines / 97 bytes**
  (byte-identical, confirmed with `od -c`), while a `--with-http_status_validation` build returns
  **10 lines / 265 bytes** (the 4 baseline lines plus `nginx_status_1xx_total` … `_5xx_total` and
  `nginx_status_validation_rejections_total`).

### QA-fix re-verification for F-PERF-1 and Info-1

After the four domain phases were approved, a QA testing pass raised one CRITICAL performance finding
(F-PERF-1) and one INFO finding (Info-1). Both were remediated within the file sets already owned by
**Phase 2 — Backend Architecture** (the C sources) and **Phase 4 — Documentation & Release** (the
traceability matrix, decision log, API and migration docs); those phases were re-reviewed and their
verdicts re-confirmed `APPROVED` against the amended code.

- **F-PERF-1 (CRITICAL, Performance) — RESOLVED.** The reason-only registry plus a per-response
  `ngx_sprintf("%03ui ", status)` in the header filter exceeded the < 2% latency budget on a
  `return 200;` endpoint (reproduced at **+2.26% CPU-time/req** vs. the stock `07a11cf77` baseline).
  Remediation: intern the full `"NNN reason"` wire literal in the registry (`line`/`line_len`), expose
  it via `ngx_http_status_line()`, and emit it from the header filter with a **single `ngx_copy`** —
  exactly the stock mechanism; `ngx_http_status_reason()` still derives the bare phrase by skipping the
  `"NNN "` prefix, so the registry remains the single source and the row stays 16 bytes (table 864 B).
  Re-verified: clean `-Werror` build of both variants; **byte-identical** to stock across 69 codes and
  the default error bodies; fixed CPU-time/req **+0.76%** (within budget); isolated microbench
  20.98 → 3.88 ns/op (81.5% render reduction). Files: `ngx_http_status.h`, `ngx_http_status.c`,
  `ngx_http_header_filter_module.c` (all Phase 2).
- **Info-1 (INFO, validation build) — RESOLVED.** Under `--with-http_status_validation`, a rejected
  local out-of-range `err_status` rendered a degenerate `"000"` status line. Remediation: in
  `ngx_http_send_header()` (`ngx_http_core_module.c`), route a rejected `err_status` to a uniform
  `500` through the sanctioned `ngx_http_status_set()` seam. Re-verified: the validation build renders
  `500` for `return 99/600/999` (was `000`) with a request-id-correlated rejection log; the fallback is
  a dead branch in the default build (byte-identity preserved) and **unreachable for upstream**
  requests (the `r->upstream` guard makes `ngx_http_status_set()` always return `NGX_OK`), so the
  upstream pass-through stays byte-for-byte for standard, non-standard, and out-of-range (999) backend
  codes in both variants. File: `ngx_http_core_module.c` (Phase 2).

Both fixes preserve every binding invariant listed above; each modified file remains within its
originally assigned domain phase, so the phase partition is unchanged and the overall verdict stands.

**Final Verdict: APPROVED**

## Coverage Checklist

All **35** changed files (11 created + 24 updated) are each assigned to **exactly one** phase — no
omissions, no duplicates.

| # | File | Op | Phase |
|---|------|----|:-----:|
| 1 | `auto/options` | UPDATED | 1 |
| 2 | `auto/sources` | UPDATED | 1 |
| 3 | `auto/modules` | UPDATED | 1 |
| 4 | `src/http/ngx_http_status.h` | CREATED | 2 |
| 5 | `src/http/ngx_http_status.c` | CREATED | 2 |
| 6 | `src/http/ngx_http.h` | UPDATED | 2 |
| 7 | `src/http/ngx_http_request.h` | UPDATED | 2 |
| 8 | `src/http/ngx_http_request.c` | UPDATED | 2 |
| 9 | `src/http/ngx_http_core_module.c` | UPDATED | 2 |
| 10 | `src/http/ngx_http_header_filter_module.c` | UPDATED | 2 |
| 11 | `src/http/ngx_http_special_response.c` | UPDATED | 2 |
| 12 | `src/http/ngx_http_upstream.c` | UPDATED | 2 |
| 13 | `src/http/modules/ngx_http_static_module.c` | UPDATED | 2 |
| 14 | `src/http/modules/ngx_http_autoindex_module.c` | UPDATED | 2 |
| 15 | `src/http/modules/ngx_http_not_modified_filter_module.c` | UPDATED | 2 |
| 16 | `src/http/modules/ngx_http_range_filter_module.c` | UPDATED | 2 |
| 17 | `src/http/modules/ngx_http_slice_filter_module.c` | UPDATED | 2 |
| 18 | `src/http/modules/ngx_http_gzip_static_module.c` | UPDATED | 2 |
| 19 | `src/http/modules/ngx_http_image_filter_module.c` | UPDATED | 2 |
| 20 | `src/http/modules/ngx_http_mp4_module.c` | UPDATED | 2 |
| 21 | `src/http/modules/ngx_http_flv_module.c` | UPDATED | 2 |
| 22 | `src/http/modules/ngx_http_dav_module.c` | UPDATED | 2 |
| 23 | `src/http/modules/ngx_http_stub_status_module.c` | UPDATED | 2 |
| 24 | `src/http/v2/ngx_http_v2_filter_module.c` | UPDATED | 2 |
| 25 | `docs/refactor/traceability_matrix.md` | CREATED | 3 |
| 26 | `docs/refactor/decision_log.md` | CREATED | 3 |
| 27 | `docs/observability/observability.md` | CREATED | 3 |
| 28 | `docs/observability/status_metrics_dashboard.json` | CREATED | 3 |
| 29 | `docs/api/status_codes.md` | CREATED | 4 |
| 30 | `docs/migration/status_code_api.md` | CREATED | 4 |
| 31 | `docs/presentation/executive_summary.html` | CREATED | 4 |
| 32 | `CHANGES` | CREATED | 4 |
| 33 | `mkdocs.yml` | UPDATED | 4 |
| 34 | `README.md` | UPDATED | 4 |
| 35 | `CODE_REVIEW.md` | CREATED | 4 |

**Per-phase totals:** Phase 1 = 3 · Phase 2 = 21 · Phase 3 = 4 · Phase 4 = 7 → **35 total**
(11 created + 24 updated). Partition is complete and disjoint.
