"""
main.py — C/LLVM IR 기반 APE 파이프라인 엔트리포인트

Usage:
    python frontend/main.py [options] FILE [FILE ...]

    FILE: .c 또는 .ll 파일. .c 는 clang-14 로 컴파일 후 파이프라인 실행.

Options:
    --plugin PATH          libLoopAnnotatedTrace.so 경로
    --apex-cache PATH      apex-cache 실행 파일 경로
    --cache PATH           cache.yaml 경로
    --output DIR           APEX-Cache report 출력 디렉터리
    --ape-only             APE JSON 생성까지만 실행
"""

import argparse
import subprocess
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).parent))

_PLUGIN_CANDIDATES = (
    _REPO_ROOT / "frontend" / "build" / "libLoopAnnotatedTrace.so",
    _REPO_ROOT / "build" / "libLoopAnnotatedTrace.so",
)
_DEFAULT_APEX_CACHE = _REPO_ROOT / "build" / "apex-cache"
_DEFAULT_CACHE = _REPO_ROOT / "settings" / "cache.yaml"
_DEFAULT_OUTPUT = _REPO_ROOT / "results"


def _default_plugin() -> Path:
    """존재하는 기본 plugin 후보를 반환한다."""
    for candidate in _PLUGIN_CANDIDATES:
        if candidate.exists():
            return candidate
    return _PLUGIN_CANDIDATES[0]


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


def _to_ll(path: Path, total_steps: int) -> Path:
    """입력이 .c면 컴파일, .ll이면 그대로 반환."""
    if path.suffix == ".c":
        print(f"  [1/{total_steps}] clang-14 컴파일 중...", end=" ", flush=True)
        ll = compile_c(path)
        print(f"완료 → {ll.name}")
        return ll
    return path


def _ape_path(ll_path: Path) -> Path:
    """foo_g.ll → foo_g_ape.json"""
    return ll_path.with_suffix("").parent / (ll_path.stem + "_ape.json")


def _json_candidates(ll_path: Path) -> list:
    """pass build 버전에 따라 가능한 JSON 출력 경로 후보를 반환한다."""
    stem_path = ll_path.with_suffix("")
    return [
        stem_path.parent / (stem_path.name + "_ape.json"),
        stem_path.parent / (stem_path.name + "_lat.json"),
    ]


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
    for candidate in _json_candidates(abs_ll):
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        f"opt-14 실행 후 JSON 출력이 생성되지 않았습니다: {out_json}")


def run_apex_cache(json_path: Path, apex_bin: Path, cache_yaml: Path,
                   output_dir: Path, verbose: bool = False,
                   no_color: bool = False) -> None:
    """APE JSON을 APEX-Cache에 전달해 report를 생성한다."""
    cmd = [
        str(apex_bin.resolve()),
        "run",
        str(json_path.resolve()),
        "--cache",
        str(cache_yaml.resolve()),
        "--output",
        str(output_dir.resolve()),
    ]
    if verbose:
        cmd.append("--verbose")
    if no_color:
        cmd.append("--no-color")
    subprocess.run(cmd, check=True, cwd=_REPO_ROOT)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="C/LLVM IR → APE JSON → APEX-Cache reports"
    )
    parser.add_argument("files", nargs="+", metavar="FILE", help=".c 또는 .ll 파일")
    parser.add_argument(
        "--plugin",
        default=str(_default_plugin()),
        metavar="PATH",
        help="플러그인 .so 경로",
    )
    parser.add_argument(
        "--apex-cache",
        default=str(_DEFAULT_APEX_CACHE),
        metavar="PATH",
        help=f"apex-cache 실행 파일 경로 (기본값: {_DEFAULT_APEX_CACHE})",
    )
    parser.add_argument(
        "--cache",
        default=str(_DEFAULT_CACHE),
        metavar="PATH",
        help=f"cache.yaml 경로 (기본값: {_DEFAULT_CACHE})",
    )
    parser.add_argument(
        "--output",
        default=str(_DEFAULT_OUTPUT),
        metavar="DIR",
        help=f"APEX-Cache report 출력 디렉터리 (기본값: {_DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--ape-only",
        action="store_true",
        help="APE JSON 생성까지만 실행",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="apex-cache run --verbose 전달",
    )
    parser.add_argument(
        "--no-color",
        action="store_true",
        help="apex-cache run --no-color 전달",
    )
    args = parser.parse_args()

    plugin = Path(args.plugin)
    if not plugin.exists():
        print(f"오류: 플러그인을 찾을 수 없습니다: {plugin}", file=sys.stderr)
        print("  빌드 후 다시 시도하거나 --plugin 으로 경로를 지정하세요.", file=sys.stderr)
        sys.exit(1)
    apex_bin = Path(args.apex_cache)
    cache_yaml = Path(args.cache)
    output_dir = Path(args.output)
    if not args.ape_only:
        if not apex_bin.exists():
            print(f"오류: apex-cache 실행 파일을 찾을 수 없습니다: {apex_bin}",
                  file=sys.stderr)
            print("  APEX-Cache 빌드 후 다시 시도하거나 --apex-cache 를 지정하세요.",
                  file=sys.stderr)
            sys.exit(1)
        if not cache_yaml.exists():
            print(f"오류: cache 설정 파일을 찾을 수 없습니다: {cache_yaml}",
                  file=sys.stderr)
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
            total_steps = 2 if args.ape_only else 3
            ll_path = _to_ll(path, total_steps)
            print(f"  [2/{total_steps}] opt-14 실행 중...", end=" ", flush=True)
            json_path = run_llvm_pass(ll_path, plugin)
            print(f"완료 → {json_path.name}", flush=True)
            if not args.ape_only:
                print(f"  [3/{total_steps}] apex-cache 실행 중...", flush=True)
                run_apex_cache(json_path, apex_bin, cache_yaml, output_dir,
                               args.verbose, args.no_color)
                print(f"완료 → {output_dir.resolve()}")
        except subprocess.CalledProcessError as e:
            stderr = e.stderr.decode(errors="replace") if e.stderr else ""
            failed = e.cmd[0] if isinstance(e.cmd, list) else e.cmd
            print(f"\n오류: {failed} 실패\n{stderr}", file=sys.stderr)
        except Exception as e:
            print(f"\n오류: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
