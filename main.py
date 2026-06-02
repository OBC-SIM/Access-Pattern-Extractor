"""
main.py — C/LLVM IR 기반 APE 파이프라인 엔트리포인트

Usage:
    python backend/main.py [--plugin PATH] FILE [FILE ...]

    FILE: .c 또는 .ll 파일. .c 는 clang-14 로 컴파일 후 파이프라인 실행.

Options:
    --plugin PATH          libLoopAnnotatedTrace.so 경로
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

_DEFAULT_PLUGIN = Path.cwd() / "build" / "libLoopAnnotatedTrace.so"


def compile_c(c_path: Path) -> Path:
    """clang-14로 .c → _g.ll 컴파일 후 .ll 경로 반환."""
    abs_c = c_path.resolve()
    out_ll = abs_c.parent / (abs_c.stem + "_g.ll")
    subprocess.run(
        [
            "clang-14",
            "-O0",
            "-Xclang",
            "-disable-O0-optnone",
            "-g",
            "-emit-llvm",
            "-S",
            "-o",
            str(out_ll),
            str(abs_c),
        ],
        check=True,
        stderr=subprocess.PIPE,
        cwd=abs_c.parent,
    )
    return out_ll


def _to_ll(path: Path) -> Path:
    """입력이 .c면 컴파일, .ll이면 그대로 반환."""
    if path.suffix == ".c":
        print(f"  [1/2] clang-14 컴파일 중...", end=" ", flush=True)
        ll = compile_c(path)
        print(f"완료 → {ll.name}")
        return ll
    return path


def _ape_path(ll_path: Path) -> Path:
    """foo_g.ll → foo_g_ape.json"""
    return ll_path.with_suffix("").parent / (ll_path.stem + "_ape.json")


def run_llvm_pass(ll_path: Path, plugin_path: Path) -> Path:
    """opt-14 실행하여 _ape.json을 생성하고 그 경로를 반환."""
    abs_ll = ll_path.resolve()
    out_json = _ape_path(abs_ll)
    subprocess.run(
        [
            "opt-14",
            f"-load-pass-plugin={plugin_path.resolve()}",
            "-passes=function(mem2reg),loop-simplify,loop-annotated-trace",
            str(abs_ll),
            "-o", "/dev/null",
        ],
        check=True,
        stderr=subprocess.PIPE,
        cwd=abs_ll.parent,
    )
    if not out_json.exists():
        raise FileNotFoundError(f"opt-14 실행 후 {out_json} 가 생성되지 않았습니다.")
    return out_json


def main() -> None:
    parser = argparse.ArgumentParser(
        description="LLVM IR → APE JSON"
    )
    parser.add_argument("files", nargs="+", metavar="FILE", help=".c 또는 .ll 파일")
    parser.add_argument(
        "--plugin",
        default=str(_DEFAULT_PLUGIN),
        metavar="PATH",
        help=f"플러그인 .so 경로 (기본값: {_DEFAULT_PLUGIN})",
    )
    args = parser.parse_args()

    plugin = Path(args.plugin)
    if not plugin.exists():
        print(f"오류: 플러그인을 찾을 수 없습니다: {plugin}", file=sys.stderr)
        print("  빌드 후 다시 시도하거나 --plugin 으로 경로를 지정하세요.", file=sys.stderr)
        sys.exit(1)

    for file_str in args.files:
        path = Path(file_str)
        if not path.exists():
            print(f"오류: 파일 없음: {path}", file=sys.stderr)
            continue
        if path.suffix not in (".c", ".ll"):
            print(f"오류: .c 또는 .ll 파일만 지원합니다: {path}", file=sys.stderr)
            continue
        try:
            ll_path = _to_ll(path)
            print(f"  [2/2] opt-14 실행 중...", end=" ", flush=True)
            json_path = run_llvm_pass(ll_path, plugin)
            print(f"완료 → {json_path.name}")
        except subprocess.CalledProcessError as e:
            stderr = e.stderr.decode(errors="replace") if e.stderr else ""
            print(f"\n오류: {e.args[0][0]} 실패\n{stderr}", file=sys.stderr)
        except Exception as e:
            print(f"\n오류: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
