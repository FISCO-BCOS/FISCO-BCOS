# Valijson

This directory supplies the header-only JsonCpp adapter used by the Web3 RPC parameter validator.
It is compiled directly and requires no package fetch or installation.

- Upstream commit: [de232f2ac3798a3c8b2cc5f3b7ebf737e1c0dbd1](https://github.com/tristanpenman/valijson/tree/de232f2ac3798a3c8b2cc5f3b7ebf737e1c0dbd1)
- Bundle command from the upstream root: `bash bundle.sh jsoncpp > valijson_jsoncpp_bundled.hpp`
- Bundle SHA-256: `941cb2287003629bcecff4dbca86340a5bbff170e98fa56c1f81272bda66abed`
- License: BSD-2-Clause. The upstream license is included verbatim.

The consuming source files enable `VALIJSON_USE_EXCEPTIONS=1`.

The RPC validator uses Valijson's regex-engine extension point with the project's existing
Boost.Regex dependency. It compiles the generated patterns at startup using ECMAScript syntax
and matches with single-line anchors. This avoids the extra per-match allocations observed
with the default standard-library regex engine on AppleClang.

Valijson compares JSON numeric bounds as IEEE-754 doubles. Adjacent integers beyond
`2^53` can compare equal, so future schemas with those bounds also need exact typed
decoding and range checks. The three pilot methods use string encodings for numeric
inputs. Their schemas bound the hex forms and their decoders enforce the target range.
