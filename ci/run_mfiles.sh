#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
binary_path="${repo_root}/build/main"

tests=(
    "test/simple_demo.m"
    "test/test1/test1.m"
    "test/test1_2/test1_2.m"
    "test/test1_3/test1_3.m"
    "test/test1_4/test1_4.m"
    "test/test1_5/test1_5.m"
)

resolve_test_arg() {
    case "$1" in
        simple_demo|simple_demo.m|test/simple_demo.m)
            printf '%s\n' "test/simple_demo.m"
            ;;
        test1|test1.m|test/test1/test1.m)
            printf '%s\n' "test/test1/test1.m"
            ;;
        test1_2|test1_2.m|test/test1_2/test1_2.m)
            printf '%s\n' "test/test1_2/test1_2.m"
            ;;
        test1_3|test1_3.m|test/test1_3/test1_3.m)
            printf '%s\n' "test/test1_3/test1_3.m"
            ;;
        test1_4|test1_4.m|test/test1_4/test1_4.m)
            printf '%s\n' "test/test1_4/test1_4.m"
            ;;
        test1_5|test1_5.m|test/test1_5/test1_5.m)
            printf '%s\n' "test/test1_5/test1_5.m"
            ;;
        -h|--help)
            cat <<'EOF'
Usage:
  ./ci/run_mfiles.sh
  ./ci/run_mfiles.sh [simple_demo|test1|test1_2|test1_3|test1_4|test1_5 ...]

Without arguments, the script builds and runs all current tests quietly,
printing only PASS/FAIL for each test.

With arguments, the script runs only the selected tests and shows full output.
You can also pass the corresponding .m filename or relative test path.
EOF
            exit 0
            ;;
        *)
            return 1
            ;;
    esac
}

selected_tests=()
verbose_mode=0
if (( $# == 0 )); then
    selected_tests=("${tests[@]}")
else
    verbose_mode=1
    for arg in "$@"; do
        if ! resolved_test="$(resolve_test_arg "$arg")"; then
            echo "Unknown test: ${arg}" >&2
            echo "Allowed tests: simple_demo test1 test1_2 test1_3 test1_4 test1_5" >&2
            exit 1
        fi
        selected_tests+=("${resolved_test}")
    done
fi

for test_file in "${selected_tests[@]}"; do
    if [[ ! -f "${repo_root}/${test_file}" ]]; then
        echo "Missing test file: ${repo_root}/${test_file}" >&2
        exit 1
    fi
done

cd "${repo_root}"

export HOME=/tmp
export XDG_CACHE_HOME=/tmp
export LD_LIBRARY_PATH="${repo_root}/deps/core/lib:/opt/Baltamatica/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

build_log=""
test_log=""
cleanup() {
    if [[ -n "${build_log}" && -f "${build_log}" ]]; then
        rm -f "${build_log}"
    fi
    if [[ -n "${test_log}" && -f "${test_log}" ]]; then
        rm -f "${test_log}"
    fi
}
trap cleanup EXIT

if (( verbose_mode )); then
    cmake --build build
else
    build_log="$(mktemp)"
    if ! cmake --build build >"${build_log}" 2>&1; then
        echo "BUILD FAIL"
        exit 1
    fi
fi

failures=0
test_log="$(mktemp)"

for test_file in "${selected_tests[@]}"; do
    if (( verbose_mode )); then
        printf '==> %s\n' "$test_file"
        : >"${test_log}"
        # 指定测试时显式回放程序的完整输出，确保 IR 和解释器输出都稳定显示。
        if "${binary_path}" "$test_file" >"${test_log}" 2>&1; then
            cat "${test_log}"
            printf 'PASS %s\n' "$test_file"
        else
            cat "${test_log}" >&2
            printf 'FAIL %s\n' "$test_file" >&2
            failures=$((failures + 1))
        fi
    else
        if "${binary_path}" "$test_file" >"${test_log}" 2>&1; then
            printf 'PASS %s\n' "$test_file"
        else
            printf 'FAIL %s\n' "$test_file"
            failures=$((failures + 1))
        fi
    fi
done

if (( failures != 0 )); then
    if (( verbose_mode )); then
        printf '\n%d test(s) failed.\n' "$failures" >&2
    else
        printf '\n%d test(s) failed. Re-run ./ci/run_mfiles.sh <test-name> for details.\n' \
            "$failures" >&2
    fi
    exit 1
fi

if (( verbose_mode )); then
    printf '\nSelected m-files parsed and executed successfully.\n'
fi
