#!/usr/bin/env bash
# NEXUS-OS — matrice de tests poussés (Phases 1 & 2).
# Balaie plusieurs configs (cœurs × mémoire) + répétitions (détection de races).
set -u
cd "$(dirname "$0")/../.." || exit 2

ISO="${ISO:-build/nexus-os.iso}"
[ -f "$ISO" ] || { echo "ISO manquante — 'make iso' d'abord"; exit 2; }

SMPS="${SMPS:-1 2 3 4 6 8 16}"
MEMS="${MEMS:-256 512 1024}"
REPEAT="${REPEAT:-1}"
# Répétitions supplémentaires sur une config "chaude" pour flusher les races.
STRESS_SMP="${STRESS_SMP:-8}"
STRESS_MEM="${STRESS_MEM:-512}"
STRESS_REPEAT="${STRESS_REPEAT:-10}"

pass=0; fail=0; failed_cfgs=""

run() { # smp mem tag
    local smp="$1" mem="$2" tag="$3" out
    out="$(QUIET=1 TIMEOUT=60 SMP="$smp" MEM="$mem" ISO="$ISO" \
           bash TESTS/qemu/run_phase6.sh 2>&1)"
    if echo "$out" | grep -q "PHASE 6 OK"; then
        pass=$((pass+1)); printf "  \033[32mOK\033[0m   %s\n" "$tag"
    else
        fail=$((fail+1)); failed_cfgs="$failed_cfgs [$tag]"
        printf "  \033[31mFAIL\033[0m %s\n" "$tag"
        echo "$out" | grep -E "FAIL:" | sed 's/^/       /'
    fi
}

echo "=== NEXUS-OS matrice de tests (Phases 1-6) ==="
echo "--- balayage cœurs × mémoire (x$REPEAT) ---"
for smp in $SMPS; do
    for mem in $MEMS; do
        for r in $(seq 1 "$REPEAT"); do
            run "$smp" "$mem" "smp=$smp mem=${mem}M #$r"
        done
    done
done

echo "--- stress répétition (smp=$STRESS_SMP mem=${STRESS_MEM}M x$STRESS_REPEAT) ---"
for r in $(seq 1 "$STRESS_REPEAT"); do
    run "$STRESS_SMP" "$STRESS_MEM" "stress #$r"
done

echo "==============================================="
echo "TOTAL: $((pass+fail))  |  PASS: $pass  |  FAIL: $fail"
if [ "$fail" -eq 0 ]; then
    echo "== MATRICE: TOUT VERT =="; exit 0
else
    echo "== MATRICE: ÉCHECS ->$failed_cfgs"; exit 1
fi
