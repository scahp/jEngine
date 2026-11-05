#!/bin/bash
set -euo pipefail
sample_dir="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$sample_dir/fetch-assets.py"
