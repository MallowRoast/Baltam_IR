#!/usr/bin/env bash
set -euo pipefail

# 从任意目录调用时，都以项目根目录查找配置和源码。
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd -- "$project_dir"

check=false
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    echo "用法：$0 [--check] [文件...]"
    echo "默认格式化 src、test、utils 下的 C/C++ 文件；--check 只检查。"
    echo "使用 CLANG_FORMAT 环境变量指定工具，默认 clang-format-11。"
    exit 0
fi
if [[ ${1:-} == --check ]]; then
    check=true
    shift
fi
formatter=${CLANG_FORMAT:-clang-format-11}
if ! command -v "$formatter" >/dev/null 2>&1; then
    echo "找不到格式化工具：$formatter，请安装 clang-format-11 或设置 CLANG_FORMAT。" >&2
    exit 1
fi

options=(-style=file)
if $check; then
    options+=(--dry-run -Werror)
else
    options+=(-i)
fi

if (( $# > 0 )); then
    for file in "$@"; do
        if [[ ! -f $file ]]; then
            echo "待格式化文件不存在：$file" >&2
            exit 1
        fi
    done
    "$formatter" "${options[@]}" -- "$@"
else
    # 使用空字符分隔，正确处理文件名中的空格，不扫描依赖和构建目录。
    find src test utils -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \
        -o -name '*.h' -o -name '*.hpp' \) -print0 \
        | xargs -0 -r "$formatter" "${options[@]}"
fi
