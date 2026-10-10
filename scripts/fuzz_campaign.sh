#!/usr/bin/env bash
# The fuzz campaign's runner (design #37): starts one command per shard in
# parallel, waits for all, and reports each shard as a [rex86-campaign]
# key=value line, in the job summary too when $GITHUB_STEP_SUMMARY is set.
# Any shard exiting nonzero fails the whole run.
#
# Usage:
#   fuzz_campaign.sh offset <tag>
#       Prints the seed offset of a vMAJOR.MINOR.PATCH tag:
#       (MAJOR * 10000 + MINOR * 100 + PATCH) * 10^8 (design #37, decision 3).
#   fuzz_campaign.sh run <job> <shards> <cases> <seed> <stride> -- <command...>
#       Runs <command> once per shard k = 0 .. shards-1. In its arguments
#       {CASES} becomes <cases>, {SEED} becomes <seed> + k * <stride> and
#       {SHARD} becomes k. Logs go to $CAMPAIGN_LOG_DIR (default
#       campaign-logs) as <job>-<k>.log.

set -uo pipefail

usage()
{
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

cpu_model()
{
    local model
    model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')
    if [ -z "$model" ] && command -v lscpu >/dev/null 2>&1; then
        model=$(lscpu | sed -n 's/^Model name: *//p' | head -n1)
    fi
    echo "${model:-unknown} ($(uname -m))"
}

# The lines a tool ends with: the host-comparison fuzzes' totals, the
# robustness harness's result, SST's totals and libFuzzer's last words.
result_line()
{
    grep -E 'mismatches=|violations=|result=|^Done [0-9]+ runs|^#[0-9]+.*DONE|INFO: fuzzed for|INFO: exiting|SUMMARY:|ERROR:' "$1" \
        | tail -n 2 | tr '\n' ' ' | sed 's/ *$//'
}

[ $# -ge 1 ] || usage
mode=$1
shift

if [ "$mode" = offset ]; then
    [ $# -eq 1 ] || usage
    if [[ ! "$1" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        echo "not a vMAJOR.MINOR.PATCH tag: $1" >&2
        exit 2
    fi
    echo $(( (BASH_REMATCH[1] * 10000 + BASH_REMATCH[2] * 100 + BASH_REMATCH[3]) * 100000000 ))
    exit 0
fi

[ "$mode" = run ] && [ $# -ge 7 ] && [ "$6" = -- ] || usage
job=$1 shards=$2 cases=$3 seed=$4 stride=$5
shift 6

log_dir=${CAMPAIGN_LOG_DIR:-campaign-logs}
mkdir -p "$log_dir"
cpu=$(cpu_model)
echo "[rex86-campaign] job=$job shards=$shards cases_per_shard=$cases seed=$seed stride=$stride cpu=\"$cpu\""

declare -a pids starts
for (( k = 0; k < shards; ++k )); do
    shard_seed=$(( seed + k * stride ))
    args=()
    for arg in "$@"; do
        arg=${arg//\{CASES\}/$cases}
        arg=${arg//\{SEED\}/$shard_seed}
        arg=${arg//\{SHARD\}/$k}
        args+=("$arg")
    done
    log="$log_dir/$job-$k.log"
    echo "command: ${args[*]}" > "$log"
    "${args[@]}" >> "$log" 2>&1 &
    pids[k]=$!
    starts[k]=$(date +%s)
done

failed=0
lines=()
for (( k = 0; k < shards; ++k )); do
    wait "${pids[k]}"
    status=$?
    seconds=$(( $(date +%s) - starts[k] ))
    [ "$status" -eq 0 ] || failed=1
    line="[rex86-campaign] job=$job shard=$k seed=$(( seed + k * stride )) cases=$cases exit=$status seconds=$seconds | $(result_line "$log_dir/$job-$k.log")"
    echo "$line"
    lines+=("$line")
    if [ "$status" -ne 0 ]; then
        echo "---- last lines of $log_dir/$job-$k.log ----"
        tail -n 20 "$log_dir/$job-$k.log"
    fi
done

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### $job ($([ "$failed" -eq 0 ] && echo ok || echo FAILED))"
        echo
        echo "CPU: $cpu"
        echo
        echo '```'
        printf '%s\n' "${lines[@]}"
        echo '```'
        echo
    } >> "$GITHUB_STEP_SUMMARY"
fi

echo "[rex86-campaign] job=$job result=$([ "$failed" -eq 0 ] && echo ok || echo fail)"
exit "$failed"
