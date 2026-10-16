#!/usr/bin/env bash
# Admission hook for Ludus performance device runners.
#
# The device owner installs a copy outside the runner and workspace directories,
# not writable by the runner account, and names it in the runner's .env file as
# ACTIONS_RUNNER_HOOK_JOB_STARTED. The runner executes it after a job is assigned
# and before any step runs; a non-zero exit fails the job without running it.
# Pull requests cannot change the installed copy, and workflows cannot overwrite
# the GITHUB_* variables it reads, so admission never depends on YAML a pull
# request supplies (docs/architecture/engineering-quality.md, section 7).
#
# Thanks to GitHub's "Running scripts before or after a job" and "Variables
# reference" documentation for the hook and default-variable contracts.
# https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/run-scripts
# https://docs.github.com/en/actions/reference/workflows-and-actions/variables
# Guide: docs/development/runtime-profiling.md#controlled-device-lane
set -euo pipefail

readonly repository="The-Ludus-Project/Ludus"
readonly workflow="${repository}/.github/workflows/performance.yml@refs/heads/main"

refuse() {
    echo "ludus-perf admission: refusing job: $1" >&2
    exit 1
}

[[ "${GITHUB_REPOSITORY:-}" == "$repository" ]] || refuse "repository '${GITHUB_REPOSITORY:-unset}' is not $repository"
[[ "${GITHUB_WORKFLOW_REF:-}" == "$workflow" ]] || refuse "workflow '${GITHUB_WORKFLOW_REF:-unset}' is not $workflow"
[[ "${GITHUB_REF:-}" == "refs/heads/main" ]] || refuse "ref '${GITHUB_REF:-unset}' is not refs/heads/main"
case "${GITHUB_EVENT_NAME:-}" in
    schedule | workflow_dispatch) ;;
    *) refuse "event '${GITHUB_EVENT_NAME:-unset}' is not schedule or workflow_dispatch" ;;
esac
echo "ludus-perf admission: accepted ${GITHUB_EVENT_NAME} run ${GITHUB_RUN_ID:-unknown} of ${GITHUB_WORKFLOW_REF}"
