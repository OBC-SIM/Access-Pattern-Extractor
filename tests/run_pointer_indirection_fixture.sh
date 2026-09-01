#!/bin/sh
set -eu

repo_root=$1
plugin=$2
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cd "$workdir"

clang-14 -O0 -Xclang -disable-O0-optnone -g \
  -I"$repo_root/tasks" \
  -emit-llvm -S \
  -o test_pointer_indirection.ll \
  "$repo_root/tasks/test_pointer_indirection.c"

opt-14 -load-pass-plugin "$plugin" \
  -passes='function(mem2reg),loop-simplify,loop-annotated-trace' \
  test_pointer_indirection.ll -o /dev/null

json=test_pointer_indirection_ape.json

assert_count()
{
  pattern=$1
  expected=$2
  actual=$(grep -oF "$pattern" "$json" | wc -l)
  if [ "$actual" -ne "$expected" ]; then
    echo "expected $expected occurrences of $pattern, found $actual" >&2
    exit 1
  fi
}

grep -Fq '"function":"unresolved_global_pointer"' "$json"
grep -Fq '"function":"unresolved_struct_field_pointer"' "$json"
grep -Fq '"function":"unresolved_selected_pointer"' "$json"

if grep -Fq '::temp:' "$json"; then
  echo "LAT object fields must not contain unregistered temp IDs" >&2
  exit 1
fi

# Seven Array nodes are emitted. Four resolve to canonical objects, while the
# three unresolved_* stores omit object without relying on SSA display names.
assert_count '"type":"Array"' 7
assert_count '"object":' 5
assert_count '"object":"global::global_ptr"' 1
assert_count '"object":"global::holder"' 1
assert_count '"object":"global::storage"' 2
assert_count '"object":"function:resolved_pointer_param_kernel::param:p"' 1
