#!/bin/sh
# Run after a normal, non-sanitized build. Error-exitcode makes CI fail on leaks.
set -eu
shell=$(realpath "${1:-./mysh}")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"
valgrind --leak-check=full --show-leak-kinds=definite,indirect \
  --errors-for-leak-kinds=definite,indirect --track-fds=yes --error-exitcode=99 \
  "$shell" -c 'echo hi > out; cat < out | cat; (cd /; pwd); sleep 0.01 & wait; echo done'
