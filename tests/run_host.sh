#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d /private/tmp/talko-monk-host.XXXXXX)
trap 'rm -rf "$test_build"' EXIT
cc -O2 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Ilib/MonkDSP/src \
  -c lib/MonkDSP/src/monk.c -o "$test_build/monk.o"
c++ -O2 -g -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all -Itests/host -Ilib/MonkDSP/src \
  tests/error_audio.cpp src/effects.cpp "$test_build/monk.o" -o "$test_build/error_audio"
"$test_build/error_audio" "$@"
