#! /bin/sh
#
# check that release-notes.md, the notes of the release that the build
# takes from ChangeLog.md, has something, without # before a digit or @
# before a name outside code, which GitHub would make a link to an issue
# or a mention in the notes of the release, and, if configure found
# markdownlint-cli2, MARKDOWNLINT, that it finds nothing in it; run by
# make check

set -e

notes=${srcdir:-.}/release-notes.md
[ -s "$notes" ] || {
    echo 'release-notes.md is empty' >&2
    exit 1
}
links=$(perl -ne 's/`[^`]*`//g; print "$.: $_" if /#\d|@\w/' "$notes")
if [ -n "$links" ]; then
    echo "$links" >&2
    echo 'release-notes.md: put #<digit> and @<name> in code, as `#1`' >&2
    exit 1
fi
if [ -z "${MARKDOWNLINT:-}" ]; then
    echo 'no markdownlint-cli2: the notes are made but not checked'
    exit 0
fi
"$MARKDOWNLINT" --config "${srcdir:-.}"/release-notes.markdownlint-cli2.yaml \
    "$notes"
