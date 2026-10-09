# Third-Party Notices

Salts first-party code is licensed under the Apache License 2.0.
The components below retain their upstream license terms.

| Component | Repository path | Upstream license | Notes |
| --- | --- | --- | --- |
| CRoaring | `vendor/croar/` | Apache-2.0 OR MIT | License notices are embedded in the amalgamated source. |
| minicoro | `vendor/minicoro/` | Unlicense OR MIT-0 | Upstream license copied to `vendor/minicoro/LICENSE`. |
| Monocypher | `vendor/monocypher/` | BSD-2-Clause OR CC0-1.0 | License notices are embedded in the upstream source. |
| reed/gf256 | `vendor/reed/` | MIT | See `vendor/reed/LICENSE`. |
| SDS | `vendor/sds/` | BSD-2-Clause | See `vendor/sds/LICENSE`. |
| TinyTest | `tinytest/` | MIT | Upstream attribution is retained in the TinyTest sources and README. |
| SQLite Lemon parser generator | `tools/lemon/` | Public-domain dedication | The source headers explicitly disclaim copyright. |

Dependencies downloaded by vcpkg or another package manager are not relicensed
by Salts and remain governed by their respective upstream licenses.

The local [minicoro](https://github.com/edubart/minicoro) ARM64 entry trampoline
uses a real call return address instead of the non-canonical `deaddead` sentinel.
This allows compiler-generated pointer authentication when a completed coroutine
switches back to its caller, including an optimized tail call. PAC/BTI compiler
settings remain enabled. The public coroutine completion, reset and nested-return
contracts are covered by `cmeta_coroutine_completion_test`.

CNet name normalization uses [ICU](https://icu.unicode.org/) at the pinned
vcpkg baseline's ICU 74.2 revision 6, under its
[ICU/Unicode license](https://github.com/unicode-org/icu/blob/release-74-2/LICENSE).
No upstream source is copied or modified. The SDK includes its package-manager
copyright file.
