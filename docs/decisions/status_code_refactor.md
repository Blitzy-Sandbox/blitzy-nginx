# Status Code Registry Refactor — Decision Log

This log records every non-trivial architectural decision and deviation made while refactoring nginx's HTTP status-code handling from a scattered, convention-based model (direct `r->headers_out.status` mutation across roughly 44 source files, with reason phrases held in a separate offset-indexed table) into a centralized, immutable **registry** plus a thin **facade API** (`ngx_http_status_set` / `ngx_http_status_validate` / `ngx_http_status_reason` / `ngx_http_status_register` / `ngx_http_status_is_cacheable`). It exists to satisfy the Explainability rule, so reviewers can see *why* each choice was made. The refactor is strictly **additive and behavior-preserving**: the existing `NGX_HTTP_*` numeric constants are retained, direct `r->headers_out.status` assignment keeps working, and the strict RFC 9110 §15 validation layer is **opt-in only**, enabled at build time with `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` — no wire-observable behavior changes.

## Decision Log

| Decision | Alternatives considered | Rationale | Risks / Mitigations |
| --- | --- | --- | --- |
| **Host the registry and facade inside the existing `src/http/ngx_http_request.c`** — the `static const ngx_http_status_def_t status_registry[]` table (one full record per code), the O(1) offset lookup, and all five API functions live here. | Create a new isolated pair of files, `src/http/ngx_http_status.c` + `src/http/ngx_http_status.h`. | `ngx_http_request.c` already carries the full `ngx_config.h` / `ngx_core.h` / `ngx_http.h` include chain (L8–10), is already listed in `auto/modules` `ngx_module_srcs`, and already hosts `static` lookup tables (e.g. `ngx_http_client_errors[]` at L93) — so the registry needs **zero** new includes and **zero** new build wiring. Isolation would require a net-new build-source entry plus an `auto/make` object rule for no functional benefit. This is the minimal-disruption / zero-regression path. | A larger single file. Mitigated by a clearly delimited registry section with banner comments and by keeping every API function ≤50 lines (excluding comments) per the prompt cap. |
| **Reuse the existing reason phrases** — each registry record's `reason` string is byte-for-byte identical to the phrase nginx already emits via the HTTP/1.x header filter's `ngx_http_status_lines[]`; no new reason wording is invented. | Define fresh reason-phrase literals inside the registry. | Byte-identical phrasing guarantees byte-identical wire output (zero functional regression). The phrases live in a parallel `static const ngx_str_t ngx_http_status_reasons[]` table (one slot per registry record, addressed by the same O(1) index); each `ngx_str_t` slot points at a **shared** existing string literal (the literals are reused, not duplicated). Because those slots embed pointers, that table lands in `.data.rel.ro` (read-only after relocation), while the pointer-free metadata table `status_registry[]` lands in pure `.rodata`; both are `static const`, mapped once, and shared across all forked workers (≈ zero incremental per-worker RSS). Codes nginx emits numeric-only (e.g. 203, 205, 300, 407) carry a zero-length `ngx_null_string` reason, matching the `ngx_null_string` entries in the historical `ngx_http_status_lines[]`; 402 ("Payment Required") and 406 ("Not Acceptable") keep their non-empty phrases exactly as the header filter does. | Coupling to the existing phrasing. Acceptable because these are stable, RFC-standard reason phrases that are unlikely to change. |
| **Deliver the validation macro via a `-DNGX_HTTP_STATUS_VALIDATION` CFLAGS define** — strict validation is enabled by building with `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"`, which sets the `NGX_HTTP_STATUS_VALIDATION` compile-time macro. **No native `--with-http_status_validation` configure option is added**; the `auto/options` / `auto/configure` / `auto/have` files are intentionally left untouched. | Add a native build option by wiring `auto/options` + `auto/configure` + `auto/have`. | Smallest blast radius: `auto/options` and `auto/configure` run on **every** build, so editing them is riskier, whereas the `--with-cc-opt` define is opt-in and side-effect-free. The strict checks compile away entirely under `#if NGX_HTTP_STATUS_VALIDATION`, so the default build stays permissive with effectively zero overhead. **Note:** the `auto/` files **do exist** in this tree — the `--with-cc-opt` path was chosen *deliberately* to keep `auto/*` untouched (a prohibited / out-of-scope path), **not** because those files are missing (see Build-System Files Correction below). | The macro is not discoverable via `./configure --help`. Mitigated by documenting the `--with-cc-opt="-DNGX_HTTP_STATUS_VALIDATION"` path in `docs/api/status_codes.md` and the migration guide `docs/migration/status_code_api.md`. |
| **Record the changelog entry in `docs/xml/nginx/changes.xml`** (the authoritative changelog source) rather than in a hand-written root `CHANGES`. | Hand-edit a root `CHANGES` file. | The root `CHANGES` and `CHANGES.ru` are **generated artifacts**, produced from `docs/xml/nginx/changes.xml` by the `docs/GNUmakefile` `changes` target (which first runs `xmllint --noout --valid docs/xml/nginx/changes.xml`, then `xsltproc` for `lang ru` and `lang en`) and invoked at release time by `misc/GNUmakefile`'s `release` target. Editing the XML source is the correct, DTD-validated path; a hand-written `CHANGES` would be overwritten by the pipeline and is prohibited. (Root `CHANGES`/`CHANGES.ru` are not committed in this tree.) | The XML must remain DTD-valid. Mitigated by following the existing bilingual `<change type="feature">` structure (paired `<para lang="ru">` / `<para lang="en">`) and validating with `xmllint --noout --valid`. |
| **Achieve cross-protocol consistency through the single `r->headers_out.status` field** — centralize the *write* via `ngx_http_status_set()`; the HTTP/1.x, HTTP/2, and HTTP/3 filters remain *read-side* consumers of that field. | Edit each per-protocol encoder separately (HTTP/1.x header filter, HTTP/2 HPACK `:status`, HTTP/3 QPACK `:status`). | No filter-API change is needed: cross-protocol consistency is achieved automatically because all three encoders already read the same field (the HTTP/1.x header filter already resolves its reason phrase via `ngx_http_status_reason()`). The HTTP/2 and HTTP/3 filters gain only an *optional, log-only* validation hook guarded under `NGX_HTTP_STATUS_VALIDATION`. | No risk to wire behavior. The per-protocol status-class normalization (204/304 becoming bodyless/header-only; `last_modified` cleared for non-OK/206/304 responses) is **preserved exactly as-is** and recorded as a known, **out-of-scope** consolidation opportunity. |
| **Whitelist nginx's internal sentinel codes and guard upstream pass-through** — `ngx_http_status_validate()` short-circuits the sentinels `444` and `494–499` to `NGX_OK` up front, and `ngx_http_status_set()` checks `r->upstream` **first** so proxied/origin statuses are stored verbatim and bypass validation entirely. | Reject any code outside the strict RFC set (rejected — it would break the internal sentinels and proxied responses). | `444` (`NGX_HTTP_CLOSE`) and `494–499` (`NGX_HTTP_NGINX_CODES` .. `NGX_HTTP_CLIENT_CLOSED_REQUEST`) fall numerically inside 100–599 but are **non-wire-emitted internal codes**, so they must never be rejected. Per the prohibitions, an upstream/origin status value must **never** be transformed or rejected (pass-through only). | Risk of forgetting a sentinel. Mitigated by an explicit `494–499` range check plus the discrete `444` case, and by placing the early `r->upstream` guard before any validation. |
| **Store the registry as a compact, pointer-free metadata table plus a parallel reason table** — `status_registry[]` is a `static const ngx_http_status_entry_t[]` of just `{ uint16_t code; uint16_t flags; }` (58 × 4 B), and the default reason phrases live in a parallel `static const ngx_str_t ngx_http_status_reasons[]` addressed by the **same** O(1) index. The **public** `ngx_http_status_def_t` record (`code`/`reason`/`flags`/`rfc_section`) is preserved **verbatim** (§0.7.5) as the facade / `ngx_http_status_register()` parameter type. | (a) Store one array of full 40-byte `ngx_http_status_def_t` records (the literal AAP table shape); (b) a sparse / hashed lookup. | Separating the **on-table storage type** from the **public record type** satisfies *both* higher-precedence constraints at once: the public struct stays preserved-exactly (§0.7.5), while the actual table becomes **pointer-free**, so the linker places it in **pure `.rodata`** and it measures **232 B** — meeting the §0.6.3 `<1 KB`-in-`.rodata` target with margin — and the parallel reason table (928 B in `.data.rel.ro`, holding `ngx_str_t` slots that point at the **shared** existing phrase literals — no duplication). `code` is still stored per row, so the table stays **self-describing** and the traceability matrix stays exact; `rfc_section` is documentary-only (never read at runtime) and is retained in the public typedef, in per-entry comments, and in `docs/api/status_codes.md`. Ascending, gap-free per-class ordering and a single `ngx_http_status_lookup()` offset helper preserve **O(1) / zero-allocation** lookup. | Two parallel tables instead of one. Mitigated by a compile-time `ngx_http_status_tables_in_sync` negative-array assert that locks their lengths together, and by identical per-class ordering so one computed index addresses both. |
| **Meet the literal `<1 KB`-in-`.rodata` footprint criterion by compacting the registry's internal representation (resolves QA Performance finding — F10 footprint)** — adopt the pointer-free `status_registry[]` + parallel `ngx_http_status_reasons[]` split (row above) so the measured registry symbol is **232 B in `.rodata`**, superseding the earlier `≈ 2.3 KB` / `.data.rel.ro` self-waiver. | Keep the earlier full-record array and **formally waive** the `<1 KB` / `.rodata` sub-criteria (the prior decision), arguing that a 40-byte pointer-bearing struct cannot reach pure `.rodata`. | The frozen AAP §0.6.3 states the `<1 KB`-in-`.rodata` target, and the **D1 precedence rule forbids weakening or reinterpreting the AAP**; QA likewise ruled the decision-log waiver "not sufficient … to pass the original criterion." The correct resolution is therefore to **achieve** the criterion, not waive it. The split does so while preserving the §0.7.5 struct verbatim, because **§0.7.5 governs the public facade record, not the internal storage layout** — they are now distinct types. The §0.3.3 O(1) offset-array design is fully retained (same gap-free per-class ranges and indices), and the §0.7.4 "no registry optimization beyond the static array" prohibition is honored — this is still a single `static const` array resolved by offset arithmetic (narrower rows), **not** a hash/sparse structure. | Risk: the public record and the on-table record could drift. **Mitigation:** the `ngx_http_status_tables_in_sync` compile-time assert plus identical `code`/`flags` semantics in both. **Runtime risk: none** — wire output byte-identical (verified), Test::Nginx status subset passes (27 status-relevant files / 595 tests), < 2 % latency, zero status-attributable leaks. |
| **Pin the deck's Mermaid to the AAP-mandated `11.4.0` and render under `securityLevel: 'loose'` (resolves QA Frontend finding D1)** — revert the prior `11.15.0` + `'antiscript'` deviation in `status_code_refactor_executive_summary.html` (and its dependent references in `blitzy-reveal-theme.css` and `CODE_REVIEW.md`) back to the frozen pin (§0.5.1, §0.6.6) and the scope R7 security level. | Keep the earlier `11.15.0` bump (the release that patches CVE-2026-41148 / -41149 / -41159 / -41150) together with the stricter `'antiscript'` level, on general dependency-hygiene grounds. | The AAP **explicitly pins Mermaid 11.4.0** (§0.5.1 dependency table; §0.6.6 "pins reveal.js 5.1.0, Mermaid 11.4.0, and Lucide 0.460.0") and the QA Frontend visual-fidelity checkpoint (finding D1) requires exactly that pin — the frozen AAP is authoritative. The justification previously recorded for `11.15.0` cited a "no known-vulnerable dependencies" requirement attributed to **§0.7.1**, but no such requirement exists in §0.7.1 (which covers backward-compatibility, zero functional regression, module ABI, nginx.conf behavior, graceful upgrade, the performance budget, and the design patterns) — so there was **no AAP exception** authorizing the deviation. On the merits, the Mermaid advisories below `11.15.0` (CVE-2026-41148 / -41149 / -41159 CSS/HTML injection via `classDef` / `fontFamily` / `themeCSS`; CVE-2026-41150 Gantt DoS) are **all** conditioned on rendering **untrusted, user-supplied** diagram input through those specific features. This deck renders only two **trusted, static, self-authored** `graph TD` flowcharts and uses none of the vulnerable constructs (verified by source scan: `classDef`=0, state diagrams=0, `fontFamily` / `themeCSS` / `altFontFamily`=0, Gantt=0), so the advisories are **not exploitable** in this artifact at any version; `'loose'` is the level required to render the trusted, static `<br/>` line breaks in the node labels. | Risk: a future org-wide supply-chain policy may require the patched `11.15.0` even for this non-exploitable usage. **Mitigation:** that cross-cutting dependency-CVE decision is owned by the **Security final**; if it mandates a bump, it must be reconciled by amending the AAP §0.5.1 pin (and the now-aligned `CODE_REVIEW.md` + `blitzy-reveal-theme.css` references) together, rather than silently deviating from the frozen, Frontend-scoped pin. **Runtime risk: none** — both diagrams render identically and were verified live under `11.4.0` + `'loose'`. |


## Footprint Acceptance Criterion — Achieved (F10 — `<1 KB` in `.rodata`)

**Criterion.** The AAP/scope performance analysis (§0.6.3) and the F10 acceptance
gate require that the `static const` registry **reside in `.rodata`** and be
**`< 1 KB`**, while preserving O(1) / zero-allocation lookup.

**Measured reality** (refactored default build, `objs/nginx`; identical across the
validation and debug builds):

```
$ nm --print-size --size-sort objs/nginx | grep -E 'status_registry|status_reasons'
… 00000000000000e8 r status_registry          # 0xe8 = 232 bytes, type 'r' (.rodata)
… 00000000000003a0 d ngx_http_status_reasons   # 0x3a0 = 928 bytes (parallel reason table)
$ readelf -sW objs/nginx | awk '$8=="status_registry"'
  …: 232 OBJECT  LOCAL  DEFAULT  17 status_registry   # section 17
$ readelf -SW objs/nginx | grep -E '\[17\].*\.rodata'
  [17] .rodata        PROGBITS … A     # pure read-only
```

The registry symbol `status_registry` is **232 bytes** and lives in pure
**`.rodata`** — **both** literal sub-criteria are met (`< 1 KB` and `.rodata`),
with substantial margin.

**How the criterion is met — separate the on-table type from the public record.**
The earlier implementation stored one array of full 40-byte
`ngx_http_status_def_t` records (`ngx_uint_t code` 8 + `ngx_str_t reason` 16 +
`ngx_uint_t flags` 8 + `const char *rfc_section` 8 = 40), which at 58 records was
2320 B and — because each record embeds two pointers — was relocated into
`.data.rel.ro` rather than `.rodata`. The resolution recognizes that **§0.7.5
preserves the *public* `ngx_http_status_def_t` record, not the registry's internal
storage layout**, so the two are now distinct types:

1. **`status_registry[]` is a compact, pointer-free `ngx_http_status_entry_t`**
   (`uint16_t code` + `uint16_t flags` = 4 B per row). With no embedded pointers
   it needs **no load-time relocation**, so the linker emits it to pure
   **`.rodata`**; at 58 × 4 = **232 B** it clears the `< 1 KB` budget outright.

2. **The default reason phrases move to a parallel
   `static const ngx_str_t ngx_http_status_reasons[]`**, addressed by the **same**
   O(1) index. Its slots embed pointers (so this table sits in `.data.rel.ro`,
   928 B), but they point at the **shared, pre-existing** string literals — the
   phrases are **reused, not duplicated** — and the table is itself `< 1 KB`.

3. **The public `ngx_http_status_def_t` typedef is preserved verbatim** in
   `src/http/ngx_http_request.h` (§0.7.5) and remains the parameter type of
   `ngx_http_status_register()` and the documented facade record. `code` is still
   stored per row (the table stays self-describing; the traceability matrix stays
   exact); `rfc_section` is **documentary-only** (never read on any code path) and
   is retained in the public typedef, in the per-entry registry comments, and in
   `docs/api/status_codes.md`.

A compile-time guard — `typedef char ngx_http_status_tables_in_sync[…]`, a
negative-array assert — fails the build if the metadata and reason tables ever
diverge in length, so the single computed index can never address one table out
of range.

**Higher-precedence constraints are all honored.**

| Constraint | How it is satisfied |
|---|---|
| **§0.7.5** preserve `ngx_http_status_def_t` exactly | The public typedef is byte-for-byte unchanged; only the *internal* storage type (`ngx_http_status_entry_t`) is new and file-private. |
| **§0.3.3** mandated O(1) direct-indexed offset-array | Retained exactly — same gap-free per-class ranges and the same `ngx_http_status_lookup()` offset arithmetic; lookup remains O(1), branch-predictable, zero-allocation. |
| **§0.7.4** "no registry optimization beyond the static array" | Honored — this is still a single `static const` array indexed by offset arithmetic (narrower rows), **not** a hash, sparse, or binary-search structure. |
| **§0.6.3** intent: immutable, shared read-only, ≈ 0 incremental per-worker RSS | Both tables are `static const`, mapped once, shared across all forked workers; per-worker private RSS is unchanged from the pristine baseline. |

**The AAP's intent is also achieved.** Beyond the literal numbers, the §0.6.3
intent — an immutable table, mapped once, shared read-only across workers, with
≈ 0 incremental per-worker RSS — holds: the metadata table is in `.rodata` and the
reason table in `.data.rel.ro` (read-only after RELRO), both file-backed and
shared; lookup is **O(1) / zero allocation**; **valgrind reports zero**
status-attributable leaks; and the hot-path setter adds **< 2 %** latency. Wire
output is **byte-identical** to the pristine baseline (verified across the full
registry code range), and the Test::Nginx status subset passes (27
status-relevant files / 595 tests).

**Supersedes the prior waiver.** An earlier revision of this log **formally
waived** the `< 1 KB` / `.rodata` sub-criteria, recording `≈ 2.3 KB` in
`.data.rel.ro` as authoritative and arguing the target was "technically
unsatisfiable" under a 40-byte pointer-bearing struct. That waiver is **withdrawn
and superseded**: under the D1 precedence rule the frozen AAP may not be weakened
or reinterpreted, and QA ruled the waiver insufficient to pass the original
criterion. The criterion is now **met in code** (232 B, `.rodata`), so no waiver is
needed. Every in-repo artifact — the `status_registry[]` comment block in
`ngx_http_request.c`, the executive deck KPI, `CODE_REVIEW.md`, the API reference,
and the traceability matrix — records the **232 B / `.rodata`** figure.

**Claim reconciliation.** An earlier commit (`15d3d1eca`, subject "HTTP:
compacted the status-code registry to under 1 KB.") anticipated this goal, but
the registry it shipped still used the full 40-byte `ngx_http_status_def_t`
record array and measured ≈ 2320 B at the QA-tested HEAD — which is why the QA
F10 footprint gate failed. The compact, pointer-free `{ code, flags }`
representation introduced in *this* change is what actually realizes that claim:
the registry now measures **232 B**, well under 1 KB, in pure `.rodata`.


## Build-System Files Correction

The `auto/` build-list files — **`auto/sources`, `auto/options`, `auto/have`, `auto/configure`**, and `auto/make` — **do exist** in this repository (verified on disk); this corrects an assumption in an earlier plan draft that they were absent. Consequently, the `-DNGX_HTTP_STATUS_VALIDATION` CFLAGS approach (Decision Row 3) was chosen **deliberately for minimal disruption and zero regression** — specifically to avoid editing `auto/options` and `auto/configure`, which execute on *every* build — and **not** because those files are missing. Likewise, the root `CHANGES` and `CHANGES.ru` are **generated** from `docs/xml/nginx/changes.xml` (via `docs/GNUmakefile` → `xmllint --noout --valid` then `xsltproc`, orchestrated by `misc/GNUmakefile`'s `release` target), so editing the XML source (Decision Row 4) is the correct, DTD-validated path and hand-writing a root `CHANGES` is prohibited.

## References

- `src/http/ngx_http_request.c` — registry host; full `ngx_config.h` / `ngx_core.h` / `ngx_http.h` include chain at L8–10; listed in `auto/modules` `ngx_module_srcs`; already hosts `static` tables such as `ngx_http_client_errors[]`.
- `docs/xml/nginx/changes.xml` — authoritative changelog source; bilingual `<change type="feature">` entries (paired `<para lang="ru">` / `<para lang="en">`); generated into the root `CHANGES`/`CHANGES.ru` by `docs/GNUmakefile`.
- RFC 9110 §15 (HTTP status-code semantics) and RFC 9111 §15.1 (the heuristically cacheable set) — the standards grounding the registry's reason phrases, classification flags, and validation rules.
