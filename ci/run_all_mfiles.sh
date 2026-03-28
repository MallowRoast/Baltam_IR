#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
binary_path="${repo_root}/build/main"
test_dir="${repo_root}/test"

if [[ ! -x "${binary_path}" ]]; then
    echo "Missing executable: ${binary_path}" >&2
    echo "Build the project first with cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

if [[ ! -d "${test_dir}" ]]; then
    echo "Missing test directory: ${test_dir}" >&2
    exit 1
fi

mapfile -t mfiles < <(find "${test_dir}" -type f -name '*.m' | sort)

if [[ ${#mfiles[@]} -eq 0 ]]; then
    echo "No .m files found under ${test_dir}" >&2
    exit 1
fi

export LD_LIBRARY_PATH="${repo_root}/deps/core/lib:/opt/Baltamatica/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

echo "Running IR interpreter suite for ${#mfiles[@]} m-file(s)"
for mfile in "${mfiles[@]}"; do
    echo ">>> ${mfile}"
    "${binary_path}" "${mfile}"
done

echo "All test m-files parsed and executed successfully."
