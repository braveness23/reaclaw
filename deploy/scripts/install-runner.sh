#!/usr/bin/env bash
# install-runner.sh — install/upgrade the in-cluster GitHub Actions runner scale
# set for braveness23/reaclaw (label: arc-runner-reaclaw). The GitHub token is
# read from $GITHUB_PAT (or the first arg) and stored only as a k8s Secret —
# never written to git.
#
#   GITHUB_PAT=ghp_xxx deploy/scripts/install-runner.sh
#
# The PAT needs access to braveness23/reaclaw (classic: repo scope; or a
# fine-grained token with Actions + Administration read/write on that repo).
#
# Prereqs: the ARC controller is already installed in the cluster (namespace
# arc-systems) — it serves the existing omega / omega-private scale sets — and
# the reaclaw-ci-runner image has been pushed to GHCR (see
# .github/workflows/build-runner-image.yml).
set -euo pipefail

NS=arc-runners
RELEASE=arc-runner-reaclaw
SECRET=arc-runner-reaclaw-github-secret
CHART_VERSION="${CHART_VERSION:-0.14.0}"
CHART="oci://ghcr.io/actions/actions-runner-controller-charts/gha-runner-scale-set"
HERE="$(cd "$(dirname "$0")/.." && pwd)"   # deploy/

PAT="${GITHUB_PAT:-${1:-}}"
[ -n "$PAT" ] || { echo "set GITHUB_PAT (or pass token as arg 1)" >&2; exit 2; }

echo "==> Ensuring namespace $NS"
kubectl get ns "$NS" >/dev/null 2>&1 || kubectl create ns "$NS"

echo "==> Creating/updating GitHub token secret $SECRET"
kubectl -n "$NS" create secret generic "$SECRET" \
  --from-literal=github_token="$PAT" \
  --dry-run=client -o yaml | kubectl apply -f -

echo "==> helm upgrade --install $RELEASE"
helm upgrade --install "$RELEASE" "$CHART" \
  --version "$CHART_VERSION" \
  -n "$NS" \
  -f "$HERE/runner/values.yaml"

echo "==> Done. Listener:"
kubectl -n arc-systems get pods -l actions.github.com/scale-set-name="$RELEASE" 2>/dev/null || true
