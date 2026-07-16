# Security Policy

## Supported Versions

libsnobol4 versions the C core and the PHP binding independently
(`core/v*`, `php/v*`). Security fixes are provided for the latest released
minor of each component.

| Component        | Supported            |
| ---------------- | -------------------- |
| C core (`core/`) | Latest `0.x` release |
| PHP binding      | Latest `0.x` release |

Pre-1.0 releases do not carry long-term support guarantees; please track the
latest tag. Once 1.0 is released this table will be updated to a
version-range policy.

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub
issues, discussions, or pull requests.**

Instead, use one of the following private channels:

- **GitHub Security Advisories** (preferred): open a private report at
  <https://github.com/JPetsis/libsnobol4/security/advisories/new>.
- If you cannot use GitHub Security Advisories, open a minimal public issue
  asking a maintainer to contact you privately — **do not include any
  vulnerability details** in that issue.

Please include, where possible:

- A description of the vulnerability and its impact.
- The affected component (C core or PHP binding) and version/commit.
- Steps to reproduce, a proof-of-concept, or a failing test case.
- Any suggested remediation.

## Response Process

- We aim to acknowledge a report within **5 business days**.
- We will investigate, keep you informed of progress, and agree on a
  coordinated disclosure timeline.
- Once a fix is available it will be released and, where appropriate,
  credited in the `CHANGELOG.md` and the GitHub Security Advisory.

## Scope

This is a string pattern-matching library. Security-relevant classes of
issues include, but are not limited to:

- Memory-safety defects (out-of-bounds read/write, use-after-free) in the C
  core reachable from untrusted pattern or subject input.
- Uncontrolled resource consumption (excessive time or memory) triggered by
  crafted patterns or inputs.
- Any defect that allows crashing or corrupting a host process embedding the
  library.

Reports demonstrating such issues with a reproducer are especially valuable.
