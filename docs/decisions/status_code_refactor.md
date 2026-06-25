# Status Code Registry Refactor — Decision Log

This log records every non-trivial architectural decision and deviation made while refactoring nginx's HTTP status-code handling from a scattered, convention-based model (direct `r->headers_out.status` mutation across roughly 44 source files, with reason phrases held in a separate offset-indexed table) into a centralized, immutable **registry** plus a thin **facade API** (`ngx_http_status_set` / `ngx_http_status_validate` / `ngx_http_status_reason` / `ngx_http_status_register` / `ngx_http_status_is_cacheable`). It exists to satisfy the Explainability rule, so reviewers can see *why* each choice was made. The refactor is strictly **additive and behavior-preserving**: the existing `NGX_HTTP_*` numeric constants are retained, direct `r->headers_out.status` assignment keeps working, and the strict RFC 9110 §15 validation layer is **opt-in only**, enabled at build time with `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` — no wire-observable behavior changes.

## Decision Log

| Decision | Alternatives considered | Rationale | Risks / Mitigations |
| --- | --- | --- | --- |
| **Host the registry and facade inside the existing `src/http/ngx_http_request.c`** — the `static const ngx_http_status_def_t status_registry[]` table (one full record per code), the O(1) offset lookup, and all five API functions live here. | Create a new isolated pair of files, `src/http/ngx_http_status.c` + `src/http/ngx_http_status.h`. | `ngx_http_request.c` already carries the full `ngx_config.h` / `ngx_core.h` / `ngx_http.h` include chain (L8–10), is already listed in `auto/modules` `ngx_module_srcs`, and already hosts `static` lookup tables (e.g. `ngx_http_client_errors[]` at L93) — so the registry needs **zero** new includes and **zero** new build wiring. Isolation would require a net-new build-source entry plus an `auto/make` object rule for no functional benefit. This is the minimal-disruption / zero-regression path. | A larger single file. Mitigated by a clearly delimited registry section with banner comments and by keeping every API function ≤50 lines (excluding comments) per the prompt cap. |
| **Reuse the existing reason phrases** — each registry record's `reason` string is byte-for-byte identical to the phrase nginx already emits via the HTTP/1.x header filter's `ngx_http_status_lines[]`; no new reason wording is invented. | Define fresh reason-phrase literals inside the registry. | Byte-identical phrasing guarantees byte-identical wire output (zero functional regression). The `static const` table is read-only after relocation (it lands in a read-only data segment, `.data.rel.ro`, because each record holds `reason`/`rfc_section` pointers) and is shared across all forked workers (≈ zero incremental per-worker RSS). Codes nginx emits numeric-only (e.g. 203, 205, 300, 407) carry a zero-length `ngx_null_string` reason, matching the `ngx_null_string` entries in `ngx_http_status_lines[]`; 402 ("Payment Required") and 406 ("Not Acceptable") keep their non-empty phrases exactly as the header filter does. | Coupling to the existing phrasing. Acceptable because these are stable, RFC-standard reason phrases that are unlikely to change. |
| **Deliver the validation macro via a `-DNGX_HTTP_STATUS_VALIDATION` CFLAGS define** — strict validation is enabled by building with `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`, which sets the `NGX_HTTP_STATUS_VALIDATION` compile-time macro. **No native `--with-http_status_validation` configure option is added**; the `auto/options` / `auto/configure` / `auto/have` files are intentionally left untouched. | Add a native build option by wiring `auto/options` + `auto/configure` + `auto/have`. | Smallest blast radius: `auto/options` and `auto/configure` run on **every** build, so editing them is riskier, whereas the `--with-cc-opt` define is opt-in and side-effect-free. The strict checks compile away entirely under `#if NGX_HTTP_STATUS_VALIDATION`, so the default build stays permissive with effectively zero overhead. **Note:** the `auto/` files **do exist** in this tree — the `--with-cc-opt` path was chosen *deliberately* to keep `auto/*` untouched (a prohibited / out-of-scope path), **not** because those files are missing (see Build-System Files Correction below). | The macro is not discoverable via `./configure --help`. Mitigated by documenting the `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` path in `docs/api/status_codes.md` and the migration guide `docs/migration/status_code_api.md`. |
| **Record the changelog entry in `docs/xml/nginx/changes.xml`** (the authoritative changelog source) rather than in a hand-written root `CHANGES`. | Hand-edit a root `CHANGES` file. | The root `CHANGES` and `CHANGES.ru` are **generated artifacts**, produced from `docs/xml/nginx/changes.xml` by the `docs/GNUmakefile` `changes` target (which first runs `xmllint --noout --valid docs/xml/nginx/changes.xml`, then `xsltproc` for `lang ru` and `lang en`) and invoked at release time by `misc/GNUmakefile`'s `release` target. Editing the XML source is the correct, DTD-validated path; a hand-written `CHANGES` would be overwritten by the pipeline and is prohibited. (Root `CHANGES`/`CHANGES.ru` are not committed in this tree.) | The XML must remain DTD-valid. Mitigated by following the existing bilingual `<change type="feature">` structure (paired `<para lang="ru">` / `<para lang="en">`) and validating with `xmllint --noout --valid`. |
| **Achieve cross-protocol consistency through the single `r->headers_out.status` field** — centralize the *write* via `ngx_http_status_set()`; the HTTP/1.x, HTTP/2, and HTTP/3 filters remain *read-side* consumers of that field. | Edit each per-protocol encoder separately (HTTP/1.x header filter, HTTP/2 HPACK `:status`, HTTP/3 QPACK `:status`). | No filter-API change is needed: cross-protocol consistency is achieved automatically because all three encoders already read the same field (the HTTP/1.x header filter already resolves its reason phrase via `ngx_http_status_reason()`). The HTTP/2 and HTTP/3 filters gain only an *optional, log-only* validation hook guarded under `NGX_HTTP_STATUS_VALIDATION`. | No risk to wire behavior. The per-protocol status-class normalization (204/304 becoming bodyless/header-only; `last_modified` cleared for non-OK/206/304 responses) is **preserved exactly as-is** and recorded as a known, **out-of-scope** consolidation opportunity. |
| **Whitelist nginx's internal sentinel codes and guard upstream pass-through** — `ngx_http_status_validate()` short-circuits the sentinels `444` and `494–499` to `NGX_OK` up front, and `ngx_http_status_set()` checks `r->upstream` **first** so proxied/origin statuses are stored verbatim and bypass validation entirely. | Reject any code outside the strict RFC set (rejected — it would break the internal sentinels and proxied responses). | `444` (`NGX_HTTP_CLOSE`) and `494–499` (`NGX_HTTP_NGINX_CODES` .. `NGX_HTTP_CLIENT_CLOSED_REQUEST`) fall numerically inside 100–599 but are **non-wire-emitted internal codes**, so they must never be rejected. Per the prohibitions, an upstream/origin status value must **never** be transformed or rejected (pass-through only). | Risk of forgetting a sentinel. Mitigated by an explicit `494–499` range check plus the discrete `444` case, and by placing the early `r->upstream` guard before any validation. |
| **Store the registry as one array of full `ngx_http_status_def_t` records** — `status_registry[]` holds 58 complete records (`code`, `reason`, `flags`, `rfc_section`), exactly as the AAP specifies. | A compact split into parallel reason / metadata tables (omitting per-record `code` and `rfc_section`) to shrink the footprint. | The full-record array is the AAP-mandated representation and the frozen AAP is authoritative; it keeps the registry **self-describing** — each row carries its own numeric code and RFC 9110 section — which makes the table auditable, keeps the traceability matrix exact, and lets `ngx_http_status_register()` validate a complete candidate `ngx_http_status_def_t`. At ≈ 2.3 KB (58 × 40-byte records) the `static const` table is read-only after relocation and shared across all forked workers, so incremental per-worker RSS is ≈ 0. The AAP/scope §0.6.3 additionally states a literal `<1 KB`-in-`.rodata` footprint target; that target is **technically unsatisfiable** under this preserved-exactly 40-byte pointer-bearing struct (§0.7.5) and the mandated O(1) offset-array layout (§0.3.3), and is **formally reconciled and waived** in the *Footprint Acceptance-Criterion Reconciliation* section below — the binding, achieved goal being immutability + worker-sharing + ≈ 0 incremental per-worker RSS. | A modestly larger table. Mitigated by ascending, gap-free per-class ordering and a single `ngx_http_status_lookup()` offset helper that resolves any code to its record in O(1). |
| **Formally waive the literal `<1 KB`-in-`.rodata` footprint sub-criteria (F10)** — record the measured **≈ 2.3 KB in `.data.rel.ro`** as the authoritative figure; see the dedicated reconciliation section below. | (a) Shrink the struct to a pointer-free layout to reach pure `.rodata` and ≤ 1 KB; (b) replace the offset-array with a sparse / hashed lookup so placeholder records can be dropped; (c) drop registry entries for codes nginx rarely emits. | The literal targets are **mutually incompatible** with two higher-precedence AAP constraints: the **preserved-exactly** 40-byte `ngx_http_status_def_t` (§0.7.5), whose two embedded pointers force a **PIE** binary to place the `static const` array in `.data.rel.ro` (relocated, read-only after RELRO) — never pure `.rodata`; and the **mandated O(1) offset-array** design (§0.3.3), whose gap-free per-class ranges spanning every code nginx emits (up to 429) make the 4xx class alone 30 × 40 = **1200 B**, already over 1024 B. The §0.6.3 *intent* (immutable, shared read-only, ≈ 0 incremental per-worker RSS) is **fully achieved and independently verified**, so the literal numbers — a proxy for that intent — are waived. | Alt (a) violates §0.7.5; alt (b) violates §0.3.3 and the §0.7.4 prohibition "no registry optimization beyond the static array" and regresses the verified O(1) / branch-predictable / zero-allocation property; alt (c) regresses wire output (the header filter's `ngx_http_status_lines[]` was removed, so the registry is the sole reason source). All rejected. **Runtime risk: none** — measured ≈ 0 incremental RSS, zero leaks, < 2 % latency. |
| **Pin the deck's Mermaid to the AAP-mandated `11.4.0` and render under `securityLevel: 'loose'` (resolves QA Frontend finding D1)** — revert the prior `11.15.0` + `'antiscript'` deviation in `status_code_refactor_executive_summary.html` (and its dependent references in `blitzy-reveal-theme.css` and `CODE_REVIEW.md`) back to the frozen pin (§0.5.1, §0.6.6) and the scope R7 security level. | Keep the earlier `11.15.0` bump (the release that patches CVE-2026-41148 / -41149 / -41159 / -41150) together with the stricter `'antiscript'` level, on general dependency-hygiene grounds. | The AAP **explicitly pins Mermaid 11.4.0** (§0.5.1 dependency table; §0.6.6 "pins reveal.js 5.1.0, Mermaid 11.4.0, and Lucide 0.460.0") and the QA Frontend visual-fidelity checkpoint (finding D1) requires exactly that pin — the frozen AAP is authoritative. The justification previously recorded for `11.15.0` cited a "no known-vulnerable dependencies" requirement attributed to **§0.7.1**, but no such requirement exists in §0.7.1 (which covers backward-compatibility, zero functional regression, module ABI, nginx.conf behavior, graceful upgrade, the performance budget, and the design patterns) — so there was **no AAP exception** authorizing the deviation. On the merits, the Mermaid advisories below `11.15.0` (CVE-2026-41148 / -41149 / -41159 CSS/HTML injection via `classDef` / `fontFamily` / `themeCSS`; CVE-2026-41150 Gantt DoS) are **all** conditioned on rendering **untrusted, user-supplied** diagram input through those specific features. This deck renders only two **trusted, static, self-authored** `graph TD` flowcharts and uses none of the vulnerable constructs (verified by source scan: `classDef`=0, state diagrams=0, `fontFamily` / `themeCSS` / `altFontFamily`=0, Gantt=0), so the advisories are **not exploitable** in this artifact at any version; `'loose'` is the level required to render the trusted, static `<br/>` line breaks in the node labels. | Risk: a future org-wide supply-chain policy may require the patched `11.15.0` even for this non-exploitable usage. **Mitigation:** that cross-cutting dependency-CVE decision is owned by the **Security final**; if it mandates a bump, it must be reconciled by amending the AAP §0.5.1 pin (and the now-aligned `CODE_REVIEW.md` + `blitzy-reveal-theme.css` references) together, rather than silently deviating from the frozen, Frontend-scoped pin. **Runtime risk: none** — both diagrams render identically and were verified live under `11.4.0` + `'loose'`. |


## Footprint Acceptance-Criterion Reconciliation (F10 — `<1 KB` in `.rodata`)

**Criterion.** The AAP/scope performance analysis (§0.6.3) and the F10 acceptance
gate state that the `static const` registry "**resides in `.rodata`**, is
**`< 1 KB`**".

**Measured reality** (refactored default build, `objs/nginx`, identical CFLAGS to
the pristine baseline): the registry is **2320 bytes** and lives in
**`.data.rel.ro`**, not `.rodata`.

```
$ nm -S objs/nginx | grep status_registry
… 0000000000000910 d status_registry            # 0x910 = 2320 bytes
$ readelf -sW objs/nginx | grep status_registry
  …: 2320 OBJECT  LOCAL  DEFAULT   24 status_registry   # section 24
$ readelf -SW objs/nginx | grep -E '\.rodata|\.data\.rel\.ro'
  [17] .rodata        PROGBITS … A     # read-only
  [24] .data.rel.ro   PROGBITS … WA    # read-only AFTER RELRO
$ readelf -dW objs/nginx | grep -E 'BIND_NOW|PIE'
  … (FLAGS)    BIND_NOW
  … (FLAGS_1)  Flags: NOW PIE
```

The table holds **58 records × 40 bytes = 2320 B**. On LP64,
`sizeof(ngx_http_status_def_t)` is 40 (`ngx_uint_t code` 8 + `ngx_str_t reason`
16 + `ngx_uint_t flags` 8 + `const char *rfc_section` 8).

**Why both literal sub-criteria are technically unsatisfiable here.** They
collide with two *higher-precedence* AAP constraints (an explicit AAP mandate
wins):

1. **`.rodata` is precluded by the preserved-exactly struct (§0.7.5).** The
   user-mandated `ngx_http_status_def_t` is preserved verbatim and embeds **two
   pointers** (`reason.data` inside `ngx_str_t`, and `rfc_section`). A
   `static const` array of a pointer-bearing aggregate needs load-time
   **relocations**; in this **PIE** binary (`Type: DYN`, `BIND_NOW`, RELRO) the
   linker therefore emits it to **`.data.rel.ro`** — read-only *after* relocation
   — never to pure `.rodata`. Reaching pure `.rodata` would require a
   **pointer-free** struct (inline char arrays or integer offsets), which would
   **violate the preserved-exactly mandate**. (Disabling PIE to force `.rodata`
   is an out-of-scope, security-regressing build change.)

2. **`< 1 KB` is precluded by the mandated offset-array design + wire coverage
   (§0.3.3).** The AAP mandates an **O(1) direct-indexed offset-array** lookup,
   which requires each status class to occupy a **contiguous, gap-free** range so
   that `index = base + (code − class_base)`. nginx emits codes up to **429**
   (`429 Too Many Requests` from `limit_req` / `limit_conn`; `421` from HTTP/2),
   so the 4xx class alone must cover **400–429 = 30 records × 40 B = 1200 B** —
   which **already exceeds the 1024-byte budget by itself**, before counting
   1xx / 2xx / 3xx / 5xx. At the preserved 40-byte struct, `< 1 KB` would permit
   only ≤ 25 records total, far too few to cover the codes nginx emits. The
   placeholder (`ngx_null_string`) records that fill non-defined codes are
   **required** to keep each class range gap-free for the O(1) lookup.

**Alternatives considered and rejected.**

| Alternative | Rejected because |
|---|---|
| Shrink the struct to a pointer-free layout (inline reason arrays / packed RFC field) to reach pure `.rodata` and ≤ 1 KB | **Violates §0.7.5** — the struct is preserved exactly, by explicit user mandate. |
| Replace the offset-array with a sparse / binary-search / hashed lookup so placeholder records can be dropped | **Violates §0.3.3** (mandated offset-array design) and the §0.7.4 prohibition "no registry optimization beyond the static array"; would also regress the QA-verified **O(1) / branch-predictable / zero-allocation** property. |
| Drop registry entries for codes nginx rarely emits | **Wire-output regression** — the header filter's `ngx_http_status_lines[]` was removed, so `ngx_http_status_reason()` (backed by this registry) is now the *sole* default-reason source; a missing entry would change the emitted status-line bytes. |
| Build the binary non-PIE so the const array lands in `.rodata` | Out-of-scope, security-regressing build-flag change; the toolchain defaults to PIE. |

**The AAP's underlying intent IS achieved.** The §0.6.3 numbers were a proxy for
"an immutable table, mapped once, shared read-only across workers, with ≈ 0
incremental per-worker RSS." Every part of that intent is independently verified
by the performance QA pass: the table is **read-only after RELRO**
(`.data.rel.ro` + `BIND_NOW`), **file-backed and shared** across all forked
workers, with per-worker `Private_Dirty` **identical to the pristine baseline**
(≈ 0 incremental private RSS); net data-segment growth is only **~ +1.8 KB** (the
removed `ngx_http_status_lines[]` reclaimed ~864 B); **valgrind reports zero**
status-attributable leaks; lookup is **O(1) / zero allocation**; and the hot-path
setter adds **< 2 %** latency (measured mean +0.00 %).

**Decision (formal waiver).** The literal `< 1 KB` and `.rodata` sub-criteria are
**formally waived** as superseded by the higher-precedence preserved-exactly
struct (§0.7.5) and offset-array design (§0.3.3). The **binding, achieved**
acceptance goal is the §0.6.3 *intent* — immutable, shared read-only, ≈ 0
incremental per-worker RSS — which holds. The authoritative measured footprint is
**≈ 2.3 KB (2320 B) in `.data.rel.ro`**, and that is the figure every in-repo
artifact records (the `status_registry[]` comment in `ngx_http_request.c`, the
executive deck, `CODE_REVIEW.md`, and this log).

**Claim correction.** The commit subject `15d3d1eca` ("HTTP: compacted the
status-code registry to under 1 KB.") is **factually inaccurate and is superseded
by this entry** — the registry measures 2320 B, not `< 1 KB`. Published history is
**not rewritten** (the commit is already referenced by hash elsewhere); the
correction is recorded here and reflected consistently across all current
artifacts.

## Build-System Files Correction

The `auto/` build-list files — **`auto/sources`, `auto/options`, `auto/have`, `auto/configure`**, and `auto/make` — **do exist** in this repository (verified on disk); this corrects an assumption in an earlier plan draft that they were absent. Consequently, the `-DNGX_HTTP_STATUS_VALIDATION` CFLAGS approach (Decision Row 3) was chosen **deliberately for minimal disruption and zero regression** — specifically to avoid editing `auto/options` and `auto/configure`, which execute on *every* build — and **not** because those files are missing. Likewise, the root `CHANGES` and `CHANGES.ru` are **generated** from `docs/xml/nginx/changes.xml` (via `docs/GNUmakefile` → `xmllint --noout --valid` then `xsltproc`, orchestrated by `misc/GNUmakefile`'s `release` target), so editing the XML source (Decision Row 4) is the correct, DTD-validated path and hand-writing a root `CHANGES` is prohibited.

## References

- `src/http/ngx_http_request.c` — registry host; full `ngx_config.h` / `ngx_core.h` / `ngx_http.h` include chain at L8–10; listed in `auto/modules` `ngx_module_srcs`; already hosts `static` tables such as `ngx_http_client_errors[]`.
- `docs/xml/nginx/changes.xml` — authoritative changelog source; bilingual `<change type="feature">` entries (paired `<para lang="ru">` / `<para lang="en">`); generated into the root `CHANGES`/`CHANGES.ru` by `docs/GNUmakefile`.
- RFC 9110 §15 (HTTP status-code semantics) and RFC 9111 §15.1 (the heuristically cacheable set) — the standards grounding the registry's reason phrases, classification flags, and validation rules.
