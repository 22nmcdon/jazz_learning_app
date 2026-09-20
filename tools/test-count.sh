#!/usr/bin/env bash
#
# The number of tests, written where a reader looks, from the only thing that
# actually knows it.
#
# The count is quoted in the README's layout table and in the page's colophon,
# and it went stale twice because both were typed by hand and nothing asked the
# suite. CI then started checking it, which caught the drift but still left
# someone to work out the new number and edit two files - a chore that is
# exactly the shape of thing a script should do.
#
#   tools/test-count.sh           write the real count into both files
#   tools/test-count.sh --check   fail if it is not already there, change nothing
#
# The binary has to exist; building it from here would mean this script owning
# an opinion about build directories, and CI has already built it by the time
# it asks. Point JAZZ_TEST_BINARY somewhere else if yours lives elsewhere.

set -euo pipefail

cd "$(dirname "$0")/.."

binary=${JAZZ_TEST_BINARY:-build-core/tests/jazz_core_tests}
check=no

if [[ ${1:-} == "--check" ]]; then
  check=yes
elif [[ $# -gt 0 ]]; then
  echo "usage: tools/test-count.sh [--check]" >&2
  exit 2
fi

if [[ ! -x $binary ]]; then
  echo "no test binary at $binary" >&2
  echo "build one:  cmake -S . -B build-core -DJAZZ_BUILD_APP=OFF && cmake --build build-core" >&2
  exit 2
fi

# The suite's own last line. Asked for rather than counted from the source,
# because a test is a call in a file and counting calls means writing a second,
# worse test runner.
count=$("$binary" | sed -n 's|^\([0-9]*\)/[0-9]* tests passed$|\1|p')

if [[ -z $count ]]; then
  echo "$binary did not report a test count - did the suite fail?" >&2
  exit 1
fi

# Where the number is written, and in which of the two shapes. A file that
# matches neither is an error rather than a no-op: silently maintaining nothing
# is the failure this script exists to end, and it would look exactly like
# success.
declare -a files=("README.md" "web/index.html")

status=0

for file in "${files[@]}"; do
  before=$(cat "$file")
  after=$(printf '%s' "$before" \
            | sed -E -e "s/[0-9]+ unit tests/$count unit tests/g" \
                     -e "s/unit tests \([0-9]+\)/unit tests ($count)/g")

  # A here-string rather than a pipe: `grep -q` stops at the first match, which
  # hands the writer a SIGPIPE, which `pipefail` reports as the pipeline having
  # failed. It only bites on a file big enough to fill the pipe buffer before
  # grep has finished - so the page failed this and the README passed it.
  if ! grep -qE "$count unit tests|unit tests \($count\)" <<< "$after"; then
    echo "::error file=$file::no test count in this file to keep up to date" >&2
    status=1
    continue
  fi

  if [[ $before == "$after" ]]; then
    continue
  fi

  if [[ $check == yes ]]; then
    echo "::error file=$file::says a test count that is not $count - run tools/test-count.sh" >&2
    status=1
  else
    printf '%s\n' "$after" > "$file"
    echo "$file: now says $count"
  fi
done

if [[ $status -eq 0 && $check == yes ]]; then
  echo "both files say $count, which is what the suite reports"
fi

exit $status
