# World of Kenshi — Jalon 2 : personnages côté serveur et entrée directe en jeu

## Contexte

Le jalon 1 a livré : serveur dédié, réseau, bouton **Multiplayer** dans le menu, lecture de la position
du joueur, et **apparition de vrais personnages Kenshi** pilotée par le réseau (spawn vérifié en jeu).
Aeglen a fixé une règle ferme (spec mise à jour, commit `docs(spec): server-side characters only…`) :
**les personnages n'existent que côté serveur, aucune sauvegarde locale n'est jamais utilisable en
multijoueur, pas de contournement « dev »**. Conséquence : plus aucun test multijoueur en jeu n'est
possible tant que « Se connecter » ne fait pas entrer directement dans la partie du serveur.

**Résultat visé (définition de « ça marche ») :** je clique Multiplayer → je sélectionne un serveur →
« Se connecter ». La 1re fois, l'éditeur de personnage de Kenshi s'ouvre et mon perso est enregistré
sur le serveur ; les fois suivantes, je retrouve directement mon perso là où je l'ai laissé. Je ne
touche jamais une sauvegarde locale. Le bot suiveur m'apparaît dans cette partie.

Repo : `D:\Projects\WorldOfKenshi` (branche `feat/m1`, à continuer sur `feat/m2`). Kenshi 1.0.68.
Spec : `docs/superpowers/specs/2026-11-12-world-of-kenshi-design.md`. Notes RE : `docs/re/*.md`.

## Règles qui s'appliquent partout

- Notre code uniquement dans le repo ; `docs/re/` = adresses/offsets + explications dans nos mots, jamais de pseudo-code copié.
- Rien de l'ABI VC2010 ne traverse sans shim (`client/src/ui/mygui_abi.*` pour `std::string` 32 octets).
- Toute fonction du jeu appelée : garde de prologue + appel protégé SEH (modèle : `client/src/game/spawner.cpp`).
- TDD pour tout code pur (protocole, base serveur, sérialisation) ; vérification en jeu pour le reste.
- Sauvegarde dans `D:\Backups` avant toute opération qui touche les fichiers de Kenshi ou les saves.
- Contenu désigné par **stringID FCS** sur le réseau (race, faction…), jamais d'index numérique.

## Tâches

### 0. Spike : authentification Steam côté serveur (décision avant la base)
L'identité (SteamID) est la clé de la base ; sans preuve, n'importe qui prend le perso d'un autre.
- Client : obtenir SteamID + ticket via `steam_api64.dll` déjà chargé par Kenshi (exports flat `SteamAPI_*`).
- Serveur : tester `SteamGameServer` (Steamworks SDK, `BeginAuthSession`, connexion anonyme, appid 233860).
- **Repli** si non viable : jeton par serveur généré à la 1re connexion et stocké côté client (`WorldOfKenshi\tokens.ini`), lié au SteamID déclaré.
- Livrable : `docs/m2-auth.md` (choix + raison), ledger.

### 1. Protocole v2 (`shared/include/wok/protocol.hpp`, tests `shared/tests/protocol_tests.cpp`)
`WOK_PROTOCOL_VERSION = 2`. Ajouts :
- `Hello` : + ticket d'auth (octets, longueur bornée).
- `HelloAck` : + `hasCharacter`.
- `CharacterData` (serveur → client) : nom, race (stringID), apparence (blob opaque borné), position, yaw.
- `CreateCharacter` (client → serveur) : mêmes champs sans position (le serveur choisit le point d'apparition).
- `RejectReason::ModMismatch` réservé (spec §7.6), non utilisé encore.
Tests : roundtrip, troncature, bornes du blob, nom non terminé.

### 2. Base de personnages côté serveur
- Dépendance vcpkg `sqlite3`. `server/include/wok/character_store.hpp` : `load(steamId)`, `create(steamId, data)`,
  `savePosition(steamId, pos, yaw)` ; implémentation SQLite (`server/src/character_store.cpp`), tests sur base `:memory:`.
- `SessionManager` (`server/src/session.cpp`) : après Hello authentifié → `HelloAck{hasCharacter}` + `CharacterData` ;
  `CreateCharacter` → stockage puis `CharacterData` ; positions persistées toutes les 10 s et à la déconnexion.
- `wok_server --db world.sqlite`. Tests de session étendus (création, reconnexion, persistance de position).

### 3. RE : démarrer une partie par le code
Partir du bouton `_NewGameButton` (trouvé par suffixe) : identifier la fonction qui lance une nouvelle partie
avec un scénario de départ (GameData), et le moment où le monde est prêt (`PlayerInterface` non nul, déjà
détecté par `Spawner::inGame()`). Noter adresses/arguments dans `docs/re/new-game.md`. Outils : REA/Ghidra
(session déjà analysée), `kre.py` dans le scratchpad, sonde Ctrl+Maj.

### 4. RE : éditeur de personnage et apparence
- Classe `CharacterEditWindow` (RTTI présente) : savoir quand le joueur valide, et lire le personnage obtenu.
- Trouver comment l'apparence d'un `CharacterHuman` est stockée/sérialisée (race stringID + données d'apparence)
  pour l'envoyer au serveur et la **réappliquer** à un personnage créé par `Spawner`.
- `docs/re/appearance.md`.

### 5. Entrée en jeu côté client (`client/src/ui/multiplayer_menu.cpp`, nouveau `client/src/session_flow.*`)
Machine d'états : Déconnecté → Connexion → Authentifié →
- **a un perso** : lancer la partie (tâche 3) en sautant l'éditeur, retirer l'escouade de départ, faire apparaître
  le perso serveur (apparence, tâche 4) à sa position et le donner au joueur ;
- **pas de perso** : lancer la partie avec l'éditeur, à la validation envoyer `CreateCharacter`, puis comme ci-dessus.
Statut affiché dans la fenêtre Multiplayer. Clic sur un serveur = remplit l'adresse seulement (déjà fait).

### 6. Interdire les sauvegardes locales en multijoueur
- Connecté : masquer Continue / New Game / Load / Import (widgets trouvés par suffixe via `mygui::findBySuffix`).
- En jeu connecté : bloquer sauvegarde/sauvegarde rapide (RE de la fonction de sauvegarde, hook MinHook déjà dans vcpkg).
- Déconnexion en jeu → retour au menu principal, rien n'est écrit sur disque.

### 7. Avatars : vrai déplacement (reporté du jalon 1)
- RE `Task_Move` / `CharMovement` / seuil `sqrTeleportationThreshold` : ordre « va à » + téléportation si grand écart.
- Couper l'IA de l'avatar ; faire disparaître l'avatar au `PlayerLeft`.
- Remplacer `Spawner::setPosition` (écriture directe, écrasée par le moteur — constaté en jeu).
- Faction « Drifters » provisoire → petit mod FCS « World of Kenshi Players » (neutre).

## Fichiers clés à réutiliser
- `client/src/game/spawner.cpp` (création escouade + perso), `client/src/game/avatars.cpp` (gestion des distants),
  `client/src/core/interp.*` (interpolation), `client/src/ui/mygui_abi.*` (MyGUI + strings VC10, `findBySuffix`),
  `client/src/game/game_memory.*` (lectures sûres, `isA` RTTI), `client/src/net_client.*` (thread réseau),
  `server/src/session.cpp` (sessions), `tools/testclient/main.cpp` (bot suiveur), `tools/install.ps1`.

## Vérification
- `ctest --preset windows-debug` : tests protocole v2, base SQLite (`:memory:`), sessions étendues — tous verts.
- `wok_server --db test.sqlite` + `wok_testclient` : création, reconnexion, position persistée entre deux lancements du serveur.
- En jeu (Aeglen) : 1re connexion → éditeur → perso créé ; quitter, relancer → perso retrouvé au même endroit ;
  boutons de sauvegarde locale absents ; bot suiveur visible et qui marche (après tâche 7).
- Avant chaque test en jeu : saves copiées dans `D:\Backups`.

## Risques
- Auth Steam côté serveur peut être impossible sans clé éditeur → repli jeton (tâche 0).
- Démarrage de partie / éditeur : reverse incertain (comme l'apparition : 1–2 sessions chacun).
- Synchronisation du monde (PNJ, bâtiments) entre clients **hors jalon 2** (chaque client a son propre monde Kenshi ; seuls les persos joueurs sont partagés).
