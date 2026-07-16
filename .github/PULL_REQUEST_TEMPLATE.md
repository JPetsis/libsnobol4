<!--
Thanks for contributing to libsnobol4! Please fill out the sections below.
Keep the C core independent of PHP internals; new functionality must land in
the C core first.
-->

## Summary

<!-- What does this PR change, and why? -->

## Component(s)

- [ ] C core (`core/`)
- [ ] PHP binding (`bindings/php/`)
- [ ] Build system / CMake
- [ ] Documentation
- [ ] CI / tooling

## Related issues

<!-- e.g. "Closes #123" -->

## Checklist

- [ ] **Tests pass**: `make test` (and `make test-asan` for engine changes) is green.
- [ ] **Lint/format clean**: `make format` applied and `make lint` (clang-tidy) reports no new issues.
- [ ] **Warnings clean**: `make warnings` builds with no new warnings.
- [ ] **CHANGELOG updated**: added an entry to `CHANGELOG.md` under `[Unreleased]`
      in [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format
      (Added / Changed / Fixed / Removed). *Every merged change must add a
      changelog entry.*
- [ ] **C/PHP coupling**: if this touches search-mode performance in
      `core/src/search_meta.c`, `core/src/search_tiers.c`, or the `vm_*` TUs,
      the corresponding PHP binding paths in `bindings/php/src/` were updated
      and `ddev test-c-probe` was run. (N/A for pure C, docs, or non-hot-path changes.)
- [ ] **New C features land in the C core first** (no PHP-only functionality).
- [ ] Docs updated where relevant (`README.md`, `CONTRIBUTING.md`, header docs).

## Notes for reviewers

<!-- Anything reviewers should focus on, benchmarks, tradeoffs, follow-ups. -->
