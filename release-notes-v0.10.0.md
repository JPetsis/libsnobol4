## v0.10.0 — Multi-Architecture JIT

**Release date:** 2026-06-09

This milestone delivers a portable, multi-architecture JIT compilation system for
libsnobol4's pattern matching engine. The JIT now supports **four** CPU backends
across **six** platform targets via a shared architecture-neutral IR pipeline.

### What's New

#### JIT Neutral IR Layer (`jit-neutral-ir`)
- Architecture-neutral IR definition with 22 opcodes, virtual register model (up to 256 per region)
- VM bytecode → IR lifter covering all opcodes
- DCE (dead-code elimination) and copy-propagation optimiser passes
- `SNOBOL_JIT_DUMP_IR=1` environment variable for IR debugging
- Backend vtable (`jit_backend_t`) with `lower`, `flush_icache`, `name` registration API
- `SNOBOL_JIT_BACKEND` CMake option for backend selection

#### ARM64 JIT (macOS + Linux AArch64)
- Refactored from monolithic ARM64 emitter to IR pipeline via `jit_backend_arm64.c`
- CFG-based multi-block compilation with stub-based control flow
- Native CI runners (macOS Apple Silicon + Linux AArch64) + QEMU AArch64 job

#### ARM32 JIT (`jit-arm32`)
- Thumb-2 backend for ARMv7-A (`jit_backend_arm32.c`)
- Fixed register convention mirroring ARM64 layout
- Full AAPCS32 call-out sequence for VM helpers
- W^X code-page model; `__builtin___clear_cache` for icache coherence
- QEMU ARMv7 CI job

#### RISC-V 64 JIT (`jit-riscv64`)
- RV64I backend (`jit_backend_riscv64.c`) with all 22 IR opcodes
- Optional RV64C compressed instruction support via `SNOBOL_JIT_RV64C`
- RISC-V psABI call-out sequence with ±2 GB AUIPC+JALR range
- QEMU RISC-V 64 CI job

#### x86-64 JIT (`jit-windows-x86`)
- Dual ABI support: System V AMD64 ABI (Linux/macOS) and Microsoft x64 ABI (Windows)
- DEP-compliant: `VirtualAlloc`/`VirtualProtect` on Windows; never uses `PAGE_EXECUTE_READWRITE`
- Full instruction emitter suite: REX, ModRM, SIB encoding; JMP/Jcc rel8/rel32; CALL rel32/mem
- CI coverage: Linux (ubuntu-latest), macOS Intel (macos-13), Windows MSVC (windows-latest)

### Build System
- `SNOBOL_JIT_BACKEND` CMake option: selects `arm64`, `arm32`, `riscv64`, or `x86_64`
- `SNOBOL_JIT_RV64C` CMake option: enables RISC-V compressed instruction support
- Auto-detection of Win64 ABI from `CMAKE_SYSTEM_NAME`

### Platform Support

| Backend  | Architecture  | OS                | CI Coverage          |
|----------|---------------|-------------------|----------------------|
| `arm64`  | AArch64       | macOS, Linux      | ✅ Native + QEMU     |
| `arm32`  | ARMv7-A       | Linux             | ✅ QEMU-emulated     |
| `riscv64`| RV64GC        | Linux             | ✅ QEMU-emulated     |
| `x86_64` | x86-64        | Linux, macOS, Win | ✅ Native runners    |

### No Breaking Changes

The public C API (`snobol.h`, `compiler.h`, `vm.h`, etc.) is unchanged. All
internal refactoring is transparent to callers. Existing bytecode, patterns,
and language bindings continue to work without modification.

### Full Changelog

See [CHANGELOG.md](CHANGELOG.md) for the complete list of changes.
