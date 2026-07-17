#!/usr/bin/env bash
# NEXUS-OS — CI d'architecture : la frontière de domaine la plus dure.
# Aucun fichier de NODE-L/ ne doit référencer NODE-W/ (et réciproquement).
# Le seul contrat transverse autorisé est IPC/proto + LIBS (cf. ARCHITECTURE.md §2).
set -u
cd "$(dirname "$0")/../.." || exit 2

fail=0

# include croisé "NODE-W/..." dans NODE-L/ ?
if grep -rniE '#\s*include\s*[<"][^">]*NODE-W' NODE-L/ 2>/dev/null; then
    echo "FAIL: NODE-L/ référence NODE-W/"; fail=1
fi
if grep -rniE '#\s*include\s*[<"][^">]*NODE-L' NODE-W/ 2>/dev/null; then
    echo "FAIL: NODE-W/ référence NODE-L/"; fail=1
fi

# mention textuelle du répertoire de l'autre domaine dans un chemin d'include
if grep -rniE 'NODE-W/' NODE-L/ --include='*.c' --include='*.h' 2>/dev/null | grep -viE '^\s*//|/\*'; then
    echo "FAIL: NODE-L/ mentionne un chemin NODE-W/"; fail=1
fi
if grep -rniE 'NODE-L/' NODE-W/ --include='*.c' --include='*.h' 2>/dev/null | grep -viE '^\s*//|/\*'; then
    echo "FAIL: NODE-W/ mentionne un chemin NODE-L/"; fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "PASS: CI archi — aucune dépendance croisée NODE-L <-> NODE-W"
    exit 0
else
    echo "== CI ARCHI: ÉCHEC =="
    exit 1
fi
