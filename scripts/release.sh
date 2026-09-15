#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

fail() { printf '[release] ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf '[release] %s\n' "$*"; }

version="${1:-$(tr -d '[:space:]' < VERSION)}"
version="${version#v}"
tag="v$version"
[[ "$version" == "$(tr -d '[:space:]' < VERSION)" ]] || fail "VERSION does not match $version"
[[ "$(git rev-parse --abbrev-ref HEAD)" == main ]] || fail "release must run on main"
[[ -z "$(git status --porcelain)" ]] || fail "working tree is not clean"
command -v gh >/dev/null || fail "GitHub CLI is required"
gh auth status >/dev/null 2>&1 || fail "GitHub CLI is not authenticated"
git rev-parse "$tag" >/dev/null 2>&1 && fail "$tag already exists"
grep -Fq "# RemoteOS-SDL $tag" RELEASE-NOTES.md || fail "stale release notes"

./scripts/validate-release.sh
git pull --rebase origin main
git push origin main
sha="$(git rev-parse HEAD)"

info "waiting for Linux/macOS CI at $sha"
run_id=""
for _ in $(seq 1 90); do
    run_id="$(gh run list --workflow CI --branch main --limit 20 \
        --json databaseId,headSha --jq ".[] | select(.headSha == \"$sha\") | .databaseId" | head -1)"
    [[ -n "$run_id" ]] && break
    sleep 10
done
[[ -n "$run_id" ]] || fail "no CI run appeared"
gh run watch "$run_id" --exit-status

git tag -a "$tag" -m "RemoteOS-SDL $tag"
git push origin "$tag"
info "waiting for tagged release workflow"
release_run=""
for _ in $(seq 1 90); do
    release_run="$(gh run list --workflow Release --limit 20 \
        --json databaseId,headBranch --jq ".[] | select(.headBranch == \"$tag\") | .databaseId" | head -1)"
    [[ -n "$release_run" ]] && break
    sleep 10
done
[[ -n "$release_run" ]] || fail "no release workflow appeared"
gh run watch "$release_run" --exit-status
gh release view "$tag" >/dev/null
info "release complete: $tag"
