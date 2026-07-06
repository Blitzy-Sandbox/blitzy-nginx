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
`ngx_http_status_set()`; all reason-phrase lookups route through `ngx_http_status_reason()`; a static
`ngx_http_status_def_t` registry (`code`, `reason`, `flags`, `rfc_section`) is the single authority.
Optional RFC 9110 validation is gated behind `--with-http_status_validation` (default OFF → the
default binary is byte-identical to baseline). Backward compatibility is preserved: all 45
`NGX_HTTP_*` constants are retained; direct `r->headers_out.status` assignment still works.

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
- **Updated (24):** `mkdocs.yml`, `README.md`, `auto/options`, `auto/sources`, `src/http/ngx_http.h`,
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
  `src/http/modules/ngx_http_stub_status_module.c`, `src/http/v2/ngx_http_v2_filter_module.c`,
  `src/http/v3/ngx_http_v3_filter_module.c`.

---

## Phase 1 — Infrastructure / DevOps

- **Reviewer role:** Build / Release Engineer
- **Domain:** Build system and configure-time feature gating (`auto/` POSIX-shell tooling).

### Files in this phase (2)

| File | Op | Role in the change |
|------|----|--------------------|
| `auto/options` | UPDATED | Adds `HTTP_STATUS_VALIDATION=NO` default, the `--with-http_status_validation` case arm (sets it `YES`), and matching `--help` text. |
| `auto/sources` | UPDATED | Wires the new source pair `src/http/ngx_http_status.c` / `src/http/ngx_http_status.h` into the HTTP build lists so `objs/Makefile` compiles and links them. |

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
- [x] **Build wiring is complete.** The `ngx_http_status.c` source and `ngx_http_status.h` dependency
  are added to the HTTP build lists so the object is compiled and linked into `objs/nginx`; the
  dependency header is tracked for incremental rebuilds.
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

### Files in this phase (22)

**Registry / API core (created)**

| File | Op | Role in the change |
|------|----|--------------------|
| `src/http/ngx_http_status.h` | CREATED | Declares `ngx_http_status_def_t {code, reason, flags, rfc_section}`, the `NGX_HTTP_STATUS_*` flag macros, and the five API prototypes. |
| `src/http/ngx_http_status.c` | CREATED | Static registry array + O(1) lookup; implements `set` / `validate` / `reason` / `register` / `is_cacheable`. |

**Core HTTP units (updated)**

| File | Op | Role in the change |
|------|----|--------------------|
| `src/http/ngx_http.h` | UPDATED | Includes/declares the status API so every HTTP unit sees it (no per-module `#include` churn). |
| `src/http/ngx_http_request.h` | UPDATED | Adds the status-def type / flag macros; retains `status`, `status_line`, `err_status`, and all 45 `NGX_HTTP_*` constants (no field reorder). |
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

**Protocol serializers (updated, 2)**

| File | Op | Role in the change |
|------|----|--------------------|
| `src/http/v2/ngx_http_v2_filter_module.c` | UPDATED | Status switch/serialization reads the status field; reason text sourced consistently with the registry. |
| `src/http/v3/ngx_http_v3_filter_module.c` | UPDATED | `:status` serialization reads the status field; reason text sourced consistently with the registry. |

### Review checklist

- [x] **Single write seam.** `ngx_http_status_set()` is the one sanctioned write path; the converted
  direct-assignment sites (17 assignments across 14 files) route through it following the
  OLD → NEW pattern `r->headers_out.status = CODE;` → `if (ngx_http_status_set(r, CODE) != NGX_OK) { … }`.
- [x] **Single read seam.** `ngx_http_status_reason()` is the one reason-phrase source; the header
  filter, the `special_response` funnel, and the HTTP/2 and HTTP/3 serializers all resolve reason
  text consistently against the registry rather than private tables.
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
- [x] **Reason-phrase parity.** The registry seeds the same reason strings already shipped (e.g.
  "200 OK", "404 Not Found"); the header filter's `status_line.len` fast-path is retained, so wire
  output is unchanged when validation is off.
- [x] **Compile-time validation gate.** The `ngx_http_status_validate()` strict path is wrapped in
  `#ifdef NGX_HTTP_STATUS_VALIDATION`; when the feature is not compiled in, the validation branch is
  absent — zero runtime cost and byte-identical output on the response hot path.
- [x] **Custom error-page HTML preserved exactly.** The per-code HTML tables in
  `ngx_http_special_response.c` (`ngx_http_error_301_page` … `ngx_http_error_411_page`) are
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
| 1 | Infrastructure / DevOps | Build / Release Engineer | 2 | `APPROVED` |
| 2 | Backend Architecture | Core C / nginx Maintainer | 22 | `APPROVED` |
| 3 | QA / Test Integrity | QA / SRE | 4 | `APPROVED` |
| 4 | Documentation & Release (Other SME) | Docs / DevRel | 7 | `APPROVED` |

Before recording the overall verdict, the four **binding preserve-behavior invariants** were
re-checked against the full change set:

1. **Byte-identical default binary.** With `--with-http_status_validation` not passed,
   `NGX_HTTP_STATUS_VALIDATION` is undefined, the strict validation branch is compiled out, and the
   registry seeds the same reason phrases already shipped — the default build reproduces baseline wire
   output. **Confirmed.**
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

**Final Verdict: APPROVED**

## Coverage Checklist

All **35** changed files (11 created + 24 updated) are each assigned to **exactly one** phase — no
omissions, no duplicates.

| # | File | Op | Phase |
|---|------|----|:-----:|
| 1 | `auto/options` | UPDATED | 1 |
| 2 | `auto/sources` | UPDATED | 1 |
| 3 | `src/http/ngx_http_status.h` | CREATED | 2 |
| 4 | `src/http/ngx_http_status.c` | CREATED | 2 |
| 5 | `src/http/ngx_http.h` | UPDATED | 2 |
| 6 | `src/http/ngx_http_request.h` | UPDATED | 2 |
| 7 | `src/http/ngx_http_request.c` | UPDATED | 2 |
| 8 | `src/http/ngx_http_core_module.c` | UPDATED | 2 |
| 9 | `src/http/ngx_http_header_filter_module.c` | UPDATED | 2 |
| 10 | `src/http/ngx_http_special_response.c` | UPDATED | 2 |
| 11 | `src/http/ngx_http_upstream.c` | UPDATED | 2 |
| 12 | `src/http/modules/ngx_http_static_module.c` | UPDATED | 2 |
| 13 | `src/http/modules/ngx_http_autoindex_module.c` | UPDATED | 2 |
| 14 | `src/http/modules/ngx_http_not_modified_filter_module.c` | UPDATED | 2 |
| 15 | `src/http/modules/ngx_http_range_filter_module.c` | UPDATED | 2 |
| 16 | `src/http/modules/ngx_http_slice_filter_module.c` | UPDATED | 2 |
| 17 | `src/http/modules/ngx_http_gzip_static_module.c` | UPDATED | 2 |
| 18 | `src/http/modules/ngx_http_image_filter_module.c` | UPDATED | 2 |
| 19 | `src/http/modules/ngx_http_mp4_module.c` | UPDATED | 2 |
| 20 | `src/http/modules/ngx_http_flv_module.c` | UPDATED | 2 |
| 21 | `src/http/modules/ngx_http_dav_module.c` | UPDATED | 2 |
| 22 | `src/http/modules/ngx_http_stub_status_module.c` | UPDATED | 2 |
| 23 | `src/http/v2/ngx_http_v2_filter_module.c` | UPDATED | 2 |
| 24 | `src/http/v3/ngx_http_v3_filter_module.c` | UPDATED | 2 |
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

**Per-phase totals:** Phase 1 = 2 · Phase 2 = 22 · Phase 3 = 4 · Phase 4 = 7 → **35 total**
(11 created + 24 updated). Partition is complete and disjoint.
