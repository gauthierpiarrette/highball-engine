#!/usr/bin/env bash
# Run from the engine checkout; apply the same pinned x64 sources in check and build jobs.
set -euo pipefail
url=$(python3 -c 'import json; print(json.load(open("inputs.json"))["wine"]["url"])')
sha=$(python3 -c 'import json; print(json.load(open("inputs.json"))["wine"]["sha256"])')
subdir=$(python3 -c 'import json; print(json.load(open("inputs.json"))["wine"]["subdir"])')
curl -fsSL --retry 3 -o crossover-sources.tar.gz "$url"
echo "$sha  crossover-sources.tar.gz" | shasum -a 256 -c -
mkdir -p src && tar -xzf crossover-sources.tar.gz -C src
root=$PWD
cd "src/$subdir"
for p in "$root"/patches/*.patch; do
  echo "applying ${p##*/}"
  patch -p1 --fuzz=0 < "$p"
done
