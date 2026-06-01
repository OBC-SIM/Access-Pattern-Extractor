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
  -o test_struct_g.ll \
  "$repo_root/tasks/test_struct.c"

opt-14 -load-pass-plugin "$plugin" \
  -passes='function(mem2reg),loop-simplify,loop-annotated-trace' \
  test_struct_g.ll -o /dev/null

json=test_struct_g_lat.json

grep -q '"schema_version":2' "$json"
grep -q '"name":"o.items.x"' "$json"
grep -q '"name":"o.items.y"' "$json"
grep -q '"object":"function:struct_field_access::param:o"' "$json"
grep -q '"indices":\["i"\]' "$json"
grep -q '"Outer"' "$json"
grep -q '"S"' "$json"
grep -q '"name":"items"' "$json"
grep -q '"offset":8' "$json"
grep -q '"shape":\[4\]' "$json"
grep -q '"source_type":"double"' "$json"
