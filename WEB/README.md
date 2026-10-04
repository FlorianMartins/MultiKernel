# MultiKernel — banc de test web

Deux façons de tester MultiKernel dans un navigateur / une VM.

## Option A — Serveur QEMU + console web (recommandé, testé)

Un petit serveur Python (stdlib only) boote l'**ISO MultiKernel dans le vrai QEMU** côté serveur
et diffuse la console série vers le navigateur (Server-Sent Events), avec entrée clavier
vers le shell Node-L.

```sh
make iso                 # produit build/nexus-os.iso
make web                 # lance le serveur -> http://127.0.0.1:8080
# (ou : python3 WEB/server.py --port 8080 --smp 4 --mem 512)
```

Ouvre `http://127.0.0.1:8080`, clique **Booter** : tu vois le boot des 7 phases en direct,
les pastilles de phase s'allument, et tu peux taper des commandes shell (`help`, `echo …`,
`ps`, `exit`) dans la fenêtre du bas (le shell Node-L tourne en ring 3 pendant le bringup).

- Vrai QEMU, vrai MultiKernel (pas d'émulateur approximatif).
- Aucune dépendance externe (Python 3 + QEMU suffisent).
- Idéal pour héberger un « service web » de démo/test (derrière un reverse-proxy).

Endpoints : `POST /boot`, `POST /reset`, `POST /input` (`{"data":"…"}`),
`GET /stream` (SSE), `GET /status`.

## Option B — 100 % navigateur via v86 (serverless, à assets)

Pour une démo **sans serveur** (MultiKernel émulé en WASM directement dans l'onglet), on utilise
[v86](https://github.com/copy/v86). v86 et ses BIOS sont des assets externes (non inclus) :

```sh
# récupérer les assets v86 dans WEB/v86/ (libv86.js, v86.wasm, seabios.bin, vgabios.bin)
#   depuis https://github.com/copy/v86 (dossier build/ d'une release)
# puis servir le dossier WEB/ en statique :
python3 -m http.server --directory WEB 8080
```

`WEB/v86.html` charge l'émulateur et boote `build/nexus-os.iso`. Plus léger à héberger
(fichiers statiques) mais v86 est un émulateur : le timing/perf n'est pas représentatif
(cf. la leçon TCG de la Phase 7). À réserver au « wow » de démo partageable.

> Note : un Artifact claude.ai ne peut pas charger v86 (CSP bloque les hosts externes et
> l'ISO ~12 Mo) — l'option B se sert depuis ton propre hébergement.

## Sécurité

Le serveur (Option A) exécute QEMU localement et n'écoute que sur `127.0.0.1` par défaut.
Pour l'exposer, mets-le **derrière un reverse-proxy authentifié** : `/input` envoie des
frappes à la VM, et `/boot` lance un process QEMU — à ne pas ouvrir tel quel sur Internet.
