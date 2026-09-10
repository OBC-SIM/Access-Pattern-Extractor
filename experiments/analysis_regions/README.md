# R1 compiler-boundary experiment

This standalone experiment validates the boundary route chosen by
[the R1 contract](../../../docs/analysis-regions-v1.md). It is not a production
region frontend: source AST validation, region LAT emission, multi-task binding
and cache analysis remain R2 work. It is not linked into the product build.

Region directives use the same uppercase snake case as `APE_ANALYZE` and
`APE_INLINE`:

```c
#pragma APE_ANALYZE_BEGIN
/* Complete statements or loops to analyze. */
#pragma APE_ANALYZE_END
```

Both directives take no arguments. The fixed region name is `APE_ANALYZE`.

The `frontend` directory is the Access-Pattern-Extractor Git submodule. This
experiment was added on its existing `main` checkout at `0e40b4c`; the parent
YARDA checkout is `feat/cpp-cache-hierarchy-rd` at `ba414a7`. No branch, commit,
index or submodule pointer was changed by the R1 work.

## Reproduce

Dependencies: the existing YARDA build, CMake, GTest, LLVM 14 development files,
Clang 14, and matching Clang development headers/`libclang-cpp.so.14`.
On Ubuntu the additional headers are supplied by `libclang-14-dev`.

From the repository root:

```sh
cmake --build build -j2
cmake -S frontend/experiments/analysis_regions -B /tmp/yarda-r1-build \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/yarda-r1-build -j2
ctest --test-dir /tmp/yarda-r1-build --output-on-failure
/tmp/yarda-r1-build/region_capture_tests --gtest_brief=1
```

If Clang headers have been extracted without a system installation, add
`-DREGION_CLANG_INCLUDE_DIR=<extraction>/usr/lib/llvm-14/include` to configuration.
CMake resolves Clang from the LLVM tools directory or `PATH` and uses that same
path for the in-memory driver and native fixture compilation. Override it with
`-DREGION_CLANG_EXECUTABLE=/path/to/clang-14`, keeping the executable and linked
Clang/LLVM 14 libraries compatible. To use a root build in another directory,
set `-DREGION_APE_PLUGIN=/path/to/libLoopAnnotatedTrace.so`; its default is the
repository's `build/libLoopAnnotatedTrace.so`.

The local run used `/tmp/yarda-r1-QsqOAi/clang-dev` for that extraction and
`/tmp/yarda-r1-QsqOAi/build` for the standalone build. No system package was
installed. Temporary paths and logs are evidence of this run, not dependencies
of future reproductions.

CTest saves regenerated LLVM IR, text snapshots and existing whole-function LAT
under `<build>/observations/`. The root build's `libLoopAnnotatedTrace.so` is used
only to check compatibility with the current extractor. Generated files stay
outside the source directory.

## Components and limitations

| File | Responsibility |
| --- | --- |
| `pragma_probe.cpp` | Clang pragma registry; inject private annotation expressions |
| `region_capture.cpp` | Check one expected IR pair, tag enclosed instructions, erase markers |
| `compile_probe.cpp` | In-memory C-to-IR, closed optimization-test option list and fixed normalization |
| `ir_probe.cpp` | Observe global load/store membership or capture an already-created experimental IR |
| `region_capture_test.cpp` | Fourteen boundary tests: complete loops, empty identity and rejection reasons |
| `helpers/region_test_support.hpp` | Verified LLVM fixtures and exact rejection assertions |
| `run_probe.cmake` | Compile fixtures and assert exact membership snapshots |
| `check_pipeline_guard.cmake` | Reject optimization/LTO, plugin and instrumentation options before output |
| `check_normalization.cmake` | Observe the untagged induction phi created by promotion |
| `check_optimization.cmake` | Reproduce optimization failures and marker interference |
| `check_lat.cmake` | Assert unchanged whole-function LAT and explicit loop/index expectations |
| `check_global_values.cmake` | Check folded constants, outside value reuse and selected global loads |
| `check_output_errors.cmake` / `helpers/limit_output.sh` | On Unix, force write failures and require diagnostic exit 1 from both probes |

The probe expects one region in one function. It does not implement the source
AST restrictions of the contract. Its dominance checks cannot by themselves
reject all source-level partial statements, conditional selections or invalid
annotations. `ir_probe capture` intentionally accepts unsafe optimized IR for
comparison; it is not the proposed public region input path. R2 must retain the
source-side expected manifest rather than infer expectations from surviving IR.

The in-memory prototype uses the same Clang `EmitLLVMOnlyAction` route selected
for R2. Its optional argument is restricted to `-O0/-O1/-O2/-O3/-Os/-Oz/-Ofast`,
`-flto`, `-flto=full` and `-flto=thin`, solely to exercise the optimization/LTO
guard. Only `-O0` succeeds. Every other optional argument, including Clang/pass
plugins, sanitizers, coverage and profiling flags, is rejected before entering
Clang. There is no arbitrary driver-option pass-through. The probe
produces tagged **whole-function IR**, not a selected LAT. Existing LLVM helpers
still process the complete function, preserving external bound/index dependencies.

Membership covers reachable instructions enclosed by the markers. It is captured
before normalization and is not a complete post-normalization instruction set.
Promotion creates an untagged induction `phi`; new control-flow instructions can
also lack tags. `ir_probe inspect` observes global load/store membership only;
`check_normalization.cmake` separately asserts the created phi's lack of a tag.
This is a frozen observation of R1's Clang/LLVM 14.0.0 pipeline, not a requirement
that R2 leave new phi nodes untagged. R2 tests must check selected loop/access
semantics; they must not inherit the negative tag assertion. A change to the R1
toolchain or normalization requires explicitly reviewing and updating its snapshot.
R2 must retain selected loop headers/access descriptors and reconstruct loops
from the whole normalized function, including untagged values. The descriptor
validation in the contract is not implemented by this R1 probe.

The default pragma-handling and standalone frontend extension points are
documented by [Clang 14 plugins](https://releases.llvm.org/14.0.0/tools/clang/docs/ClangPlugins.html)
and [Clang 14 LibTooling](https://releases.llvm.org/14.0.0/tools/clang/docs/LibTooling.html).
The optimization conclusions below come from local executable observations.

## Observations (2026-09-09, LLVM/Clang 14.0.0, x86-64)

`boundary.c` has separate `before`, `inside` and `after` global objects, a named
`APE_ANALYZE` function, a complete loop, and externally defined constant
`bound=3`/`offset=1`. Native Clang preserves the function annotation but discards
the two region pragmas. The plugin introduces `llvm.annotation.i32` calls.
Capture removes both calls before `function(mem2reg),loop-simplify`.

The normalized membership is exactly:

```text
function region_probe
store before outside
load inside region
store inside region
store after outside
```

Stripping debug information before capture produces the same membership. The
in-memory C++ path produces the same result. The native, captured and in-memory
paths produce byte-identical **whole-function** LAT with the existing extractor.
Independent assertions check `start=0`, `bound=3`, `step=1`, `index=i+1`, the
canonical `global::inside` object, and load-before-store order. The six selected
dynamic accesses are specified in the contract and still need R2 producer tests.
`empty.c` retains selection identity without tagging either outside store.

The M1–M4 follow-up (2026-09-10) also observes `global_values.c`. Its global
`const` bound/offset fold to 3/1 and generate no loads under the recorded Clang
14.0.0 pipeline. File-scope `const int` is not a C integer constant expression;
this folding is an observed compiler behavior, not a C language guarantee.
A value loaded from `external_value` before the region remains usable inside it
without selecting that outside load. A separate sample taken inside each
iteration is selected:

```text
function global_values_probe
load external_value outside
store before outside
load external_value region
load inside region
store inside region
store after outside
```

The canonical, native and in-memory paths have identical whole-function LAT.
Explicit assertions check three iterations and the inner order: sample load,
`inside[i+1]` load, then store. The contract's selected dynamic sequence has nine
accesses; R2 must still verify those events through its producer/ELF integration.
The nine-access expectation depends on this fixed pipeline's retained IR and
recorded target; a compiler change requires rechecking the folded loads and LAT.

`dynamic_bound.c` loads an unresolved external global bound. The probe emits IR
showing `load bound region`, which establishes boundary membership only. This
fixture is deliberately not sent to the existing LAT exporter. R2 must reject
it before LAT output; successful R1 boundary capture is not evidence of supported
loop/value resolution. Global initializers or ELF addresses alone do not prove
a runtime-loaded value.

`motion.c` exposes why optimized input is excluded:

| Pipeline | Global loads of `shared_value` | Selected loads |
| --- | ---: | ---: |
| O0 capture then canonical normalization | 3 | 1 |
| Clang O2 with private annotation markers, then capture | 1 | 0 |
| Capture first, then LLVM default O2 | 1 | 0 |
| Native Clang O2 without transport | 1 | No region information |
| Clang O2 with opaque external marker calls | 3 | Not a valid region transport |

O2 reuses an outside load for the inside store; instruction metadata does not
preserve the eliminated selected access. Opaque calls themselves change visible
memory optimization. The fixed in-memory route explicitly rejects optimization,
LTO, plugin and instrumentation options without producing output. These
observations do not measure runtime speed or validate arbitrary optimizations,
CFGs, PolyBench inputs or cache outcomes.

## Verification

The initial no-op capture implementation ran all eight original tests and failed
all eight (`/tmp/yarda-r1-QsqOAi/red.xml`). The M1 UBSan rejection and M2 unreachable
membership regression each failed before their fixes. The current capture suite
passes fourteen tests; the separate CTest suite contains fifteen tests including
the compiler pipeline, normalization, global-value and Unix output-error checks.
The M5/M6 follow-up covers a real loop back edge, cross-function boundaries,
used begin/end results, boundaries inside a loop and an unreachable begin.
Every rejection asserts its reason; the empty case asserts identity, zero tagged
instructions and marker removal. These additions cover existing capture behavior.

Both probes previously aborted with SIGABRT on an output write failure. The new
Unix regression sets the child process's file-size limit to zero and ignores
SIGXFSZ, leaving diagnostic pipes writable. It failed before the stream-error
fix and now requires exit 1 with the expected write diagnostic. Resource limits
apply only to the child process. Tool and plugin path overrides, including paths
with spaces, were also checked with the complete compiler pipeline.
The normal product build and its existing 575 CTests/329 hierarchy GTests also
passed after the B8 user commit `ba414a7`. Production code and root build targets
were not changed by this experiment.

Memory checks are reproducible with:

```sh
valgrind --vgdb=no --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=all --error-exitcode=99 \
  /tmp/yarda-r1-build/region_capture_tests --gtest_brief=1
valgrind --vgdb=no --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect,possible --error-exitcode=99 \
  /tmp/yarda-r1-build/region_compile_probe \
  frontend/experiments/analysis_regions/fixtures/boundary.c \
  /tmp/yarda-r1-build/valgrind-canonical.ll
```

Each executable explicitly calls LLVM's managed shutdown after its contexts and
actions are destroyed. This also releases the shared library's command-line
registry, which otherwise appears as still-reachable process-global memory.
The boundary tests exit with zero allocated bytes and zero errors. The compiler
probe has zero invalid accesses or definite/indirect/possible leaks; Clang 14's
`getDriverOptTable()` retains 189,411 reachable bytes in six blocks. The compiler
command reports these reachable allocations and does not count them as leaks.
The initial stricter `--errors-for-leak-kinds=all` compiler run returned 99 for
those four driver-table allocation contexts; no suppression file was used.
