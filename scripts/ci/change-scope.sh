#!/usr/bin/env bash
# Decide whether a pull request needs the builds and the test suite.
#
# A pull request that changes only documentation skips the heavy work: the
# build jobs still start and report, so every required check passes, but they
# do nothing. Everything else, and every push and manual run, builds.
#
# Documentation means a file under docs/ or website/, or a *.md file at the
# top of the repository, that nothing outside those reads. A file anywhere
# else is never documentation, whatever its name: resources/help/*.md is
# compiled into the app through resources.qrc.
#
# "Reads" is checked for every documentation path: any tracked file outside
# docs/ and website/ (source, tests, CMake, the resource lists, tools/,
# hal-plugin/, scripts/, ...) that names the path or its folder on a line
# that is not a comment. A top-level file's name counts on its own or after
# a path prefix that is a variable, a quote or a parenthesis
# ("${CMAKE_SOURCE_DIR}/README.md", "/CHANGELOG.md"), not as the end of a
# longer path (docs/x/README.md does not name README.md). Each folder the
# file is in, up to but not including docs/ and website/ themselves,
# counts when it is named on its own (with or without a trailing slash) or
# with a file pattern after it ("icons/%1.svg", "docs/help/*.md"), not as
# the start of another file's path: docs/architecture/a.md does not name
# the folder of docs/architecture/b.md. Files that build nothing are not
# searched: *.md files
# anywhere (they are text; one the app compiles in is named by its
# resource list, which is searched), .github/ (the release workflow and
# the pull request template; ci.yml's own checks run on every pull
# request), .gitattributes, .gitignore, CODEOWNERS, the top-level LICENSE
# texts and this script. tst_app_icon,
# for example, opens the icons in a docs/architecture design folder, so a
# change there has to run the tests. A comment that names a document (this
# is common: "see docs/architecture/...") reads nothing, so it does not
# count. Which lines are comments depends on the file's kind:
#   C, C++, Objective-C, Swift, JavaScript: "//" or "/*" before the path
#     at the start of the line or after a space, a tab or ";" (so a URL's
#     "//" does not count), or a line starting with "*" (inside a block
#     comment);
#   CMake, Python, shell, YAML, PowerShell, systemd units: "#" before the
#     path;
# (a configure template, x.in, is the kind of x);
#   anything else: no line is a comment, so every mention counts.
#
# Renames are listed as a deletion and an addition (--no-renames), so moving
# a source file into docs/ still names the source path.
#
# Run from the root of a checkout of the pull request's merge commit with at
# least two commits of history (actions/checkout fetch-depth: 2). Writes
# heavy=true or heavy=false to $GITHUB_OUTPUT, or to stdout without it, and
# says why. Anything unexpected (no parent, a failed diff, an empty list, a
# failed search) means heavy=true: a wrong "true" costs a build, a wrong
# "false" lets an untested change through.
#
#   scripts/ci/change-scope.sh <github.event_name>
#
# CI speed round, 2026-10-16; the review fixes (top-level *.md only,
# --no-renames, every path checked, the whole repository searched;
# prefixed top-level names, folders without a slash, every parent folder,
# URLs), 2026-10-17.
set -uo pipefail

out="${GITHUB_OUTPUT:-/dev/stdout}"

decide() {
    echo "heavy=$1" >> "$out"
    echo "$2"
    exit 0
}

# Prints the first line, outside the documentation itself, that names $1
# (a file, or a folder ending in "/") and is not a comment, as
# file:line:text. Status 0 if there is one, 1 if there is none, 2 if the
# search failed.
reader_of() {
    local hits status
    # A folder is searched for without its slash, so "docs/x/icons" with no
    # slash after it is found too; the awk below decides what follows.
    hits="$(git grep -n -F -e "${1%/}" -- . ':(exclude)docs/' ':(exclude)website/' \
        ':(exclude,glob)**/*.md' ':(exclude)*.md' ':(exclude).github/' \
        ':(exclude).gitattributes' ':(exclude).gitignore' ':(exclude)CODEOWNERS' \
        ':(exclude)LICENSE*' ':(exclude)scripts/ci/change-scope.sh' 2>/dev/null)"
    status=$?
    if [ "$status" -eq 1 ]; then
        return 1
    fi
    if [ "$status" -ne 0 ]; then
        return 2
    fi
    awk -v needle="$1" '
        {
            # file:line:text; the file name has no colon in this repository.
            file = $0; sub(/:.*/, "", file)
            text = $0; sub(/^[^:]*:[0-9]+:/, "", text)
            # The first mention that is a whole name.
            #  - A top-level file (no "/" in it) preceded by a path or word
            #    character is the end of a longer name, unless that is a
            #    "/" after a variable, quote, parenthesis, space or the
            #    start of the line ("${ROOT}/README.md", "/CHANGELOG.md").
            #  - A folder (searched for without its slash) counts when the
            #    line ends there or a quote, space or ")" follows, or a "/"
            #    followed by nothing that names a file, or by a file
            #    pattern (%, *, $, {, <) before the next "/". Followed by
            #    any other name character, it is the start of a longer
            #    name or the path of another file.
            folder = (substr(needle, length(needle), 1) == "/")
            word = folder ? substr(needle, 1, length(needle) - 1) : needle
            rest = text; offset = 0; at = 0
            while ((i = index(rest, word)) > 0) {
                pos = offset + i
                prev = (pos > 1) ? substr(text, pos - 1, 1) : ""
                after = substr(text, pos + length(word))
                whole = 1
                if (index(needle, "/") == 0 && prev ~ /[A-Za-z0-9_.\/-]/) {
                    whole = 0
                    if (prev == "/") {
                        prev2 = (pos > 2) ? substr(text, pos - 2, 1) : ""
                        if (prev2 == "" || prev2 ~ /[}")'"'"' \t]/) {
                            whole = 1
                        }
                    }
                }
                if (folder) {
                    if (after == "" || after ~ /^["'"'"' \t)]/) {
                        # named on its own
                    } else if (substr(after, 1, 1) == "/") {
                        tail = substr(after, 2)
                        if (tail ~ /^[A-Za-z0-9_.-]/ && tail !~ /^[^\/ \t"'"'"'`)]*[%*${<]/) {
                            whole = 0
                        }
                    } else {
                        whole = 0
                    }
                }
                if (whole) {
                    at = pos
                    break
                }
                offset = pos
                rest = substr(text, offset + 1)
            }
            if (at == 0) {
                next
            }
            before = substr(text, 1, at - 1)
            kind = "other"
            # A configure template (x.service.in, x.h.in) is its inner kind.
            sub(/\.in$/, "", file)
            if (file ~ /\.(c|cc|cpp|cxx|h|hh|hpp|hxx|m|mm|swift|js|ts|qml)$/) {
                kind = "slash"
            } else if (file ~ /(^|\/)CMakeLists\.txt$/ || file ~ /\.(cmake|py|sh|bash|ya?ml|ps1|toml|cfg|ini|service|socket|conf)$/) {
                kind = "hash"
            }
            if (kind == "slash" && (before ~ /(^|[ \t;])\/[\/*]/ || text ~ /^[ \t]*\*/)) {
                next
            }
            if (kind == "hash" && before ~ /#/) {
                next
            }
            print
            found = 1
            exit
        }
        END { exit found ? 0 : 1 }
    ' <<< "$hits"
}

if [ "${1:-}" != "pull_request" ]; then
    decide true "Not a pull request: every build runs."
fi

# The merge commit's first parent is the base branch, so this is exactly
# what the pull request changes.
if ! files="$(git diff --name-only --no-renames HEAD^1 HEAD 2>/dev/null)"; then
    decide true "Could not list the pull request's files: every build runs."
fi
if [ -z "$files" ]; then
    decide true "The pull request changes no files: every build runs."
fi

while IFS= read -r file; do
    case "$file" in
        docs/*|website/*)
            ;;
        */*)
            decide true "$file is not documentation: every build runs."
            ;;
        *.md)
            ;;
        *)
            decide true "$file is not documentation: every build runs."
            ;;
    esac
    # The file itself, and each folder it is in for files read by folder
    # (a test that opens every icon in a folder names the folder, not each
    # file), up to but not including docs/ and website/ themselves: a test
    # checks that no text it shows mentions "docs/", which reads nothing.
    # A top-level file has no folder to check.
    needles=("$file")
    dir="$(dirname "$file")"
    while [ "$dir" != . ] && [ "$dir" != docs ] && [ "$dir" != website ]; do
        needles+=("$dir/")
        dir="$(dirname "$dir")"
    done
    for needle in "${needles[@]}"; do
        reader="$(reader_of "$needle")"
        case $? in
            0) decide true "$file: $reader names $needle, so every build runs." ;;
            1) ;;
            *) decide true "$file: could not search for readers of $needle, so every build runs." ;;
        esac
    done
done <<< "$files"

decide false "Documentation only: the build jobs report without building."
