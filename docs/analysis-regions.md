# Analysis regions

`yarda_region_map` compiles C11 source to APE/MAP v2 with one selected region per
function. The optional executable shares the LLVM statement/module builders with
the legacy plugin.

```c
int data[8];

void kernel(void) {
    data[0] = 0;
#pragma APE_ANALYZE_BEGIN
    for (int i = 1; i < 4; ++i) data[i]++;
#pragma APE_ANALYZE_END
    data[0] = 1;
}
```

The region automatically selects `kernel`. Only the six inner load/store
references belong to its task. Outside statements still participate in
compilation and value resolution, but do not contribute accesses or cache state.
Empty regions are retained. A region takes precedence over `APE_ANALYZE`;
combining it with `APE_INLINE` is rejected.

## Build and run

The default build leaves this executable disabled. Enabling it requires matching
LLVM 14 and Clang 14 development headers, `clang-cpp`, and the `clang-14` driver.
On Ubuntu the additional headers are supplied by `libclang-14-dev`.

```sh
cmake -S . -B build -DYARDA_BUILD_REGION_FRONTEND=ON
cmake --build build
build/yarda_region_map kernel.c kernel.json -- -Iinclude -DBOUND=3
ctest --test-dir build --output-on-failure
```

These paths assume a standalone frontend checkout. In the parent YARDA build,
configure at the repository root and use `build/frontend/yarda_region_map`.
`YARDA_CLANG_INCLUDE_DIR` can point to separately extracted matching headers.
When GTest is available, the default build also runs the LLVM-only descriptor
tests and the legacy plugin's region-rejection CLI test without Clang headers.
The source frontend tests are enabled with the optional executable.

The compiler owns the entire `clang14-o0-region-v1` pipeline: fresh O0 IR with
debug information and O0 `optnone` disabled, source/IR boundary matching, marker
removal, `function(mem2reg),loop-simplify`, descriptor validation, and MAP export.
Loop selection uses saved header identities; newly created scalar/control
instructions need not carry tags. Pure compiler arithmetic intrinsics such as
`fmuladd` do not create references. Retained memory/call sites must preserve
their identities. Additional sites inside selected loops are rejected even
without tags; untagged accesses outside those loops remain excluded. Unused
static region functions are explicitly retained during IR generation so they
cannot disappear from the source manifest.

Allowed extra arguments are `-O0`, `-I`, `-D`, `-U`, `-isystem`, `-iquote`,
`-include`, and `--target`. The first three preprocessing options accept joined
values; `--target=value` is also accepted. Optimization, LTO, instrumentation,
plugins, response files, arbitrary Clang options and imported IR are rejected.
The `--` separator before these extra arguments is optional.

## Supported selection and output

Boundaries must be literal argument-free main-file directives surrounding
consecutive complete top-level statements in a function. Complete finite `for`
loops can be nested. Conditional selection, jumps, partial loops/statements,
header or macro boundaries, inline assembly and custom assembler symbols are
unsupported. An ordinary final return after the region is allowed. Exported
function/inline bodies must also use the supported straight-line/for-loop
structure; a helper cannot hide conditional accesses behind an inline call.
Function definitions with custom assembler symbols are rejected in headers too.

This strict domain applies to exported whole-function roots and inline helpers
even without a region. With no annotation or region, all emitted definitions
are validated and exported without gaining an analysis role. Bodies excluded
from export do not cause control-flow rejection. The source validator records
body errors and checks them against the same emitted-function selection used
by the MAP builder, before normalization and output publication.

Loop start, bound and step must resolve to constants without induction overflow.
Indices must fit the supported MAP literal/induction-variable/constant-offset
representation. Runtime global initializers are not treated as runtime values.
Pointer reinterpretation, atomics and unsupported memory intrinsics are rejected.
Runtime-loaded pointer bases are rejected before MAP publication. Globals,
local objects and formal pointer parameters retain their canonical bindings,
including parameters used by inline helpers.
Unresolved or unsupported inputs never fall back to whole-function selection.

The output preserves original function names, parameters, object identities and
layouts. Region entries add
`"analysis_scope":{"kind":"region","name":"APE_ANALYZE"}` and contain only their
selected body. The YARDA task consumer derives
`region:<function UTF-8 byte length>:<function>:APE_ANALYZE` while preserving
original object bindings. Task identity collisions are errors.
Every retained direct call, including opaque calls, carries positional `args`
and an equally sized `arg_objects` array with canonical storage IDs where known.
Selected opaque calls remain unsupported by the hierarchy consumer.

All source/IR validation finishes before output is opened. A sibling temporary
file is closed and renamed only on success; failure preserves an existing output
and removes incomplete temporary output. The legacy LLVM plugin explicitly
rejects recognizable region transport. Under `opt-14`, rejection invokes LLVM's
registered output cleanup and reports an error with exit code 1, removing the
incomplete IR output file.

For linked-address analysis, compile the same original source to ET_EXEC with
the same target, object definitions and preprocessing flags, without loading the
pragma handler. The integration fixtures use native `clang-14 -std=c11 -O0 -g
-no-pie`, with unknown-pragmas warnings disabled. No runtime global values, stack,
heap or TLS addresses are inferred from the ELF.

Parent-repository integration tests verify the six-reference boundary fixture,
the nine-reference global-value fixture, independent cold/empty tasks, and a
53-reference ATAX adaptation with fixed `2 x 3` static arrays. This small input
gate does not establish full PolyBench coverage or target-scale performance.
