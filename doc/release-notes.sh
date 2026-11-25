#! /bin/sh
#
# print the notes of the release of VERSION, in Markdown: its section of
# ChangeLog.md, with its headings one level up, as the title of the
# GitHub release, or of the dialog of the launcher, takes the place of
# the section's own; they are the notes of its GitHub release,
# release-notes.md, which the launcher shows, and, after a line
# "powwow VERSION", the message of its tag; run in the source directory,
# as doc/Makefile does:
#
#     doc/release-notes.sh VERSION
#
# A -dev VERSION takes the section of the version it leads to, such as
# 2.0.0 for 2.0.0-dev, which may still be "(unreleased)"; any other
# VERSION fails with such a section, or with none.

set -e

if [ $# -ne 1 ]; then
    echo "usage: $0 VERSION" >&2
    exit 1
fi
version=$1
base=${version%-dev}

# the header of the section, "## Changes from X to VERSION", maybe with
# more after a space
header=$(awk -v v="$base" '
             /^## Changes from / {
                 to = substr($0, index($0, " to ") + 4)
                 if (to == v || index(to, v " ") == 1) {
                     print
                     exit
                 }
             }
         ' ChangeLog.md)
if [ -z "$header" ]; then
    echo "$0: ChangeLog.md has no section for $base" >&2
    exit 1
fi
case $version,$header in
    *-dev,*) ;;
    *'(unreleased)'*)
        echo "$0: ChangeLog.md has $base as unreleased" >&2
        exit 1
        ;;
esac

# the lines after the header, up to the next section, without the empty
# lines at the start and the end
awk -v header="$header" '
    $0 == header { on = 1; next }
    on && /^## / { exit }
    !on { next }
    /^###/ { $0 = substr($0, 2) }
    NF == 0 { if (seen) ++blank; next }
    { for (; blank > 0; --blank) print ""; print; seen = 1 }
' ChangeLog.md
