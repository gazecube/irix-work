#!/usr/bin/env bash
set -e

git pull
rm -rf irix-re/build
cmake -S irix-re -B irix-re/build
cmake --build irix-re/build --target pandora_viewkit_probe
exec ./irix-re/build/viewkit/pandora_viewkit_probe
