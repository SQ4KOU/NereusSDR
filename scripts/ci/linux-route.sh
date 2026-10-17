#!/usr/bin/env bash
# Decide where this CI run's Linux build and tests go: the self-hosted
# runners (label nereus-linux) or GitHub's hosted runners.
#
# Self-hosted when all of these hold, hosted otherwise:
#   - the run builds at all (HEAVY=true; see change-scope.sh);
#   - it is a push, a manual run, or a pull request from a branch of this
#     repository. A pull request from a fork never runs on the
#     self-hosted runners: its code would run on our machine.
#   - a manual run did not ask for hosted (LINUX_RUNNERS=github-hosted);
#   - a runner with the label is online. That takes a token that may read
#     the repository's runners (RUNNER_STATUS_TOKEN, from the secret
#     NEREUS_RUNNER_STATUS_TOKEN: a fine-grained token with the
#     "Administration: read" repository permission; the workflow's own
#     token cannot list runners). Without the secret, or when the check
#     fails, the run goes to hosted: it never waits on a runner it could
#     not see.
#
# Writes route=box|hosted and legs=<JSON> to $GITHUB_OUTPUT (stdout without
# it). legs is the build job's matrix: LEGS (every leg, as JSON) as given
# for hosted, and without its Linux legs (those with "linux": true) for
# self-hosted, where the linux-box job builds and tests Linux instead.
#
# Inputs, all from the environment: HEAVY, EVENT_NAME, HEAD_REPO,
# GITHUB_REPOSITORY, LINUX_RUNNERS, RUNNER_STATUS_TOKEN, LEGS.
set -uo pipefail

out="${GITHUB_OUTPUT:-/dev/stdout}"
: "${LEGS:?LEGS must hold every build leg as JSON}"
label="nereus-linux"

decide() {
    local legs
    if [ "$1" = "box" ]; then
        legs="$(jq -c '[.[] | select(.linux != true)]' <<< "$LEGS")"
    else
        legs="$(jq -c '.' <<< "$LEGS")"
    fi
    if [ -z "$legs" ]; then
        echo "::error::LEGS is not valid JSON."
        exit 1
    fi
    {
        echo "route=$1"
        echo "legs=$legs"
    } >> "$out"
    echo "Linux: $2"
    exit 0
}

if [ "${HEAVY:-}" != "true" ]; then
    decide hosted "nothing to build, so the hosted legs report without building."
fi

case "${EVENT_NAME:-}" in
    push)
        ;;
    workflow_dispatch)
        if [ "${LINUX_RUNNERS:-}" = "github-hosted" ]; then
            decide hosted "the manual run asked for GitHub's hosted runners."
        fi
        ;;
    pull_request)
        if [ "${HEAD_REPO:-}" != "${GITHUB_REPOSITORY:-}" ]; then
            decide hosted "a pull request from a fork (${HEAD_REPO:-unknown}) builds on GitHub's hosted runners."
        fi
        ;;
    *)
        decide hosted "event ${EVENT_NAME:-unknown} builds on GitHub's hosted runners."
        ;;
esac

if [ -z "${RUNNER_STATUS_TOKEN:-}" ]; then
    echo "::warning::No NEREUS_RUNNER_STATUS_TOKEN secret, so this run cannot see whether a $label runner is online; building Linux on GitHub's hosted runners."
    decide hosted "the runner check could not run."
fi

# The reply's status and GitHub's message are printed when the check
# fails, so a token without the permission says so in the log.
body="$(mktemp)"
if ! status="$(curl -sS --retry 3 --max-time 20 -o "$body" -w '%{http_code}' \
        -H "Authorization: Bearer $RUNNER_STATUS_TOKEN" \
        -H "Accept: application/vnd.github+json" \
        -H "X-GitHub-Api-Version: 2022-11-28" \
        "https://api.github.com/repos/$GITHUB_REPOSITORY/actions/runners?per_page=100")" \
        || [ "$status" != "200" ]; then
    reason="$(jq -r '.message // empty' "$body" 2>/dev/null)"
    echo "::warning::Could not list the repository's runners (HTTP ${status:-none}${reason:+: $reason}); building Linux on GitHub's hosted runners."
    rm -f "$body"
    decide hosted "the runner check failed."
fi
runners="$(cat "$body")"
rm -f "$body"

if ! online="$(jq --arg label "$label" \
        '[.runners[] | select(.status == "online") | select(any(.labels[]; .name == $label))] | length' \
        <<< "$runners")"; then
    echo "::warning::Could not read the runner list; building Linux on GitHub's hosted runners."
    decide hosted "the runner check failed."
fi

if [ "$online" -gt 0 ]; then
    decide box "the self-hosted runners ($online online)."
fi
echo "::warning::No $label runner is online; building Linux on GitHub's hosted runners."
decide hosted "no self-hosted runner online."
