# World of Kenshi — M1 « Deux joueurs se voient marcher » — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Deux joueurs lancent Kenshi avec le mod, se connectent au même serveur dédié et voient le personnage de l'autre marcher dans leur jeu.

**Architecture :** Une DLL C++ (`wok_client.dll`) est chargée par Kenshi comme plugin OGRE (ligne ajoutée à `Plugins_x64.cfg`). Elle s'accroche à la boucle de rendu via `Ogre::Root::addFrameListener`, lit la position du personnage local grâce à des signatures trouvées par reverse engineering (REA), et l'envoie à un serveur dédié C++ (`wok_server`). Le serveur relaie les positions aux autres clients, qui font apparaître et déplacent un avatar. Le code réseau et le protocole sont dans une bibliothèque partagée (`wok_shared`), testée sans le jeu.

**Tech Stack :** C++20, CMake ≥ 3.28 + presets, MSVC 14.50 (VS 18 BuildTools), vcpkg en mode manifest (`gamenetworkingsockets`, `doctest`, `minhook`), GameNetworkingSockets (UDP), doctest pour les tests.

**Spec :** `docs/superpowers/specs/2026-11-12-world-of-kenshi-design.md`

## Global Constraints

- Nom du projet : **World of Kenshi**. Préfixe de code : `wok`, namespace `wok`. Repo : `D:\Projects\WorldOfKenshi`.
- Licence MIT. **Aucun code décompilé de Kenshi et aucun fichier du jeu dans le repo.** Seulement des signatures d'octets, des offsets et des notes écrites par nous dans `docs/re/`.
- Kenshi cible : `D:\SteamLibrary\steamapps\common\Kenshi\kenshi_x64.exe`. La version est notée dans `currentVersion.txt` (1.0.65 aujourd'hui). Toute adresse passe par un **scan de signature**, jamais par une adresse codée en dur.
- Le jeu est compilé avec MSVC 2010 (MSVCP100/MSVCR100). **Aucun type STL ne traverse la frontière avec le jeu, OGRE ou MyGUI** : ni `std::string`, ni `std::vector`, ni allocation libérée de l'autre côté. Seulement des pointeurs, des POD et des interfaces virtuelles pures.
- Serveur : compile et tourne sur Windows x64 et Linux x64. Le client est Windows x64 uniquement.
- Port UDP par défaut : **27800**. Joueurs max par défaut : **250**.
- Protocole : version `WOK_PROTOCOL_VERSION = 1`, little-endian, envoi de l'état local à **10 Hz**, interpolation des avatars avec un tampon de **100 ms**.
- La DLL ne doit **jamais faire planter Kenshi**. Si une signature est introuvable, ou si le serveur est injoignable, on écrit dans le log et on désactive la fonctionnalité concernée.
- Log client : `<Kenshi>\WorldOfKenshi\wok_client.log`. Config client : `<Kenshi>\WorldOfKenshi\wok_client.ini`.
- Sauvegarde **obligatoire dans `D:\Backups`** avant de supprimer ou d'écraser un fichier du dossier Kenshi.

## Review Focus

1. **Paquet malformé ou tronqué reçu par le serveur** : il est rejeté, le serveur ne plante pas et le client fautif est déconnecté après 3 paquets invalides. Tests dans la Tâche 2 (`decode_rejects_truncated`) et la Tâche 3 (`session_kicks_after_three_bad_packets`).
2. **Client qui disparaît sans prévenir** (crash, câble) : les autres reçoivent `PlayerLeft` après **10 s** de timeout et l'avatar disparaît. Test dans la Tâche 3 (`session_times_out_silent_player`).
3. **Version de Kenshi différente** (signature introuvable) : le mod reste chargé, il logge `signature not found: <nom>`, n'envoie pas de position, et le jeu continue normalement. Test dans la Tâche 6 (`scan_returns_null_when_absent`) et vérification manuelle dans la Tâche 7.
4. **Pas de personnage local** (menu principal, chargement, personnage mort ou escouade vide) : aucun `PlayerState` n'est envoyé et rien ne plante. Vérification manuelle dans la Tâche 8.
5. **251e joueur, mauvaise version de protocole ou joueur banni** : un `HelloAck` refusé avec la bonne raison est affiché en jeu. Tests dans la Tâche 3 (`join_rejected_when_full`, `join_rejected_on_version_mismatch`, `join_rejected_when_banned`).

---

## Structure des fichiers

```
WorldOfKenshi/
  CMakeLists.txt, CMakePresets.json, vcpkg.json, LICENSE, README.md, .gitignore
  .github/workflows/ci.yml
  shared/            # wok_shared (lib statique) — protocole + transport, sans dépendance au jeu
    include/wok/protocol.hpp     messages + encode/decode
    include/wok/bytes.hpp        ByteWriter / ByteReader
    include/wok/transport.hpp    façade GameNetworkingSockets
    src/*.cpp
    tests/*.cpp
  server/            # wok_server (exe) + wok_server_core (lib testable)
    include/wok/session.hpp      logique de sessions (pure, sans réseau)
    src/session.cpp, src/main.cpp
    tests/session_tests.cpp
  tools/testclient/  # wok_testclient (exe) — faux joueur qui marche en cercle
  tools/install.ps1  # installe la DLL dans Kenshi (sauvegarde + Plugins_x64.cfg)
  client/            # wok_client.dll — Windows uniquement
    src/plugin.cpp               dllStartPlugin / dllStopPlugin
    src/log.hpp/.cpp
    src/config.hpp/.cpp          lecture wok_client.ini
    src/ogre_abi.hpp             déclarations minimales compatibles VC10 (FrameListener, Root)
    src/frame_hook.hpp/.cpp
    src/sigscan.hpp/.cpp         scan de signatures (testé hors jeu)
    src/game/offsets.hpp         signatures + offsets (sortie de la Tâche 7)
    src/game/local_player.hpp/.cpp
    src/game/avatars.hpp/.cpp    avatars des joueurs distants + interpolation
    src/net_client.hpp/.cpp      thread réseau client
    tests/sigscan_tests.cpp, tests/interp_tests.cpp
  docs/re/           # notes de reverse (adresses, structures, méthode), écrites par nous
```

---

### Tâche 0 : Environnement de test Kenshi propre

**Files :** aucun dans le repo. Opérations sur `D:\SteamLibrary\steamapps\common\Kenshi`.

Le dossier Kenshi contient les restes des essais précédents : `RE_Kenshi.dll`, `RE_Kenshi.ini`, dossier `RE_Kenshi\`, `KenshiLib.dll`, `KenshiMP.Core.dll`, `CompressToolsLib.dll` (à vérifier), dossiers `client\` et `server\`, ainsi que les `KenshiOnline_*.log`.

- [ ] **Étape 1 :** Sauvegarder tout le dossier Kenshi (sans `data\`, qui est volumineux et sera restauré par Steam) dans `D:\Backups\Kenshi-2026-11-12\`. Vérifier que la copie existe et que le nombre de fichiers correspond.
- [ ] **Étape 2 :** Demander à Aeglen de confirmer, puis supprimer les fichiers listés plus haut. Lancer ensuite **Steam → Kenshi → Propriétés → Fichiers installés → Vérifier l'intégrité** pour obtenir un `kenshi_x64.exe` vanilla. Si RE_Kenshi avait forcé une branche bêta 1.0.65, remettre la branche par défaut si Aeglen le souhaite.
- [ ] **Étape 3 :** Lancer Kenshi via Steam, démarrer une nouvelle partie, puis quitter. Résultat attendu : le jeu tourne normalement. Noter `currentVersion.txt` et le MD5 de `kenshi_x64.exe` dans `docs/re/README.md` (créé à la Tâche 1).

### Tâche 1 : Squelette du repo et build

**Files :** Create `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `LICENSE`, `README.md`, `.gitignore`, `.github/workflows/ci.yml`, `docs/re/README.md`, ainsi que des cibles vides `shared/`, `server/` et `client/`.

**Interfaces :**
- Produces : les cibles CMake `wok_shared` (STATIC), `wok_server_core` (STATIC), `wok_server` (EXE), `wok_testclient` (EXE), `wok_client` (SHARED, uniquement si `WIN32`), et `wok_tests` (EXE doctest qui agrège tous les `tests/*.cpp` ; un `ctest` l'enregistre).
- Presets : `windows-debug`, `windows-release` (VS 18 2026 x64, toolchain vcpkg `C:/Program Files/vcpkg/scripts/buildsystems/vcpkg.cmake`) et `linux-release` (Ninja).

- [ ] **Étape 1 :** `git init`, ajouter les fichiers, `vcpkg.json` avec les dépendances `gamenetworkingsockets`, `doctest` et `minhook` (minhook uniquement sur Windows : `"platform": "windows"`). Licence MIT (© 2026 World of Kenshi contributors). Le README contient un paragraphe sur le projet et la règle « pas de code Kenshi ».
- [ ] **Étape 2 :** Ajouter un test bidon `TEST_CASE("build_sanity") { CHECK(1 + 1 == 2); }`.
- [ ] **Étape 3 :** Lancer `cmake --preset windows-debug && cmake --build --preset windows-debug && ctest --preset windows-debug`. Résultat attendu : 1 test passé, et `wok_client.dll` produit (vide).
- [ ] **Étape 4 :** La CI GitHub Actions compile `wok_shared`, `wok_server` et les tests sur `windows-latest` et `ubuntu-latest`. Le client est compilé uniquement sur Windows.
- [ ] **Étape 5 :** Commit `chore: repo skeleton, CMake presets, vcpkg, CI`.

### Tâche 2 : Protocole (`wok_shared`)

**Files :** Create `shared/include/wok/bytes.hpp`, `shared/include/wok/protocol.hpp`, `shared/src/protocol.cpp`. Test : `shared/tests/protocol_tests.cpp`.

**Interfaces :**
- Produces :
  ```cpp
  namespace wok {
  constexpr uint16_t WOK_PROTOCOL_VERSION = 1;
  using PlayerId = uint32_t;
  enum class MsgType : uint8_t { Hello = 1, HelloAck = 2, PlayerState = 3, PlayerLeft = 4 };
  enum class RejectReason : uint8_t { None = 0, ServerFull = 1, VersionMismatch = 2, Banned = 3, BadAuth = 4 };
  enum class AnimState : uint8_t { Idle = 0, Walk = 1, Run = 2 };
  struct Hello       { uint16_t protocolVersion; uint64_t steamId; char name[32]; };
  struct HelloAck    { RejectReason reason; PlayerId playerId; };   // reason==None => accepté
  struct PlayerState { PlayerId playerId; uint32_t tick; float x, y, z; float yaw; AnimState anim; };
  struct PlayerLeft  { PlayerId playerId; };
  using Message = std::variant<Hello, HelloAck, PlayerState, PlayerLeft>;
  std::vector<uint8_t> encode(const Message&);              // 1er octet = MsgType
  std::optional<Message> decode(std::span<const uint8_t>);  // nullopt si invalide
  }
  ```
  `ByteWriter` / `ByteReader` : écriture et lecture little-endian de u8, u16, u32, u64, f32 et d'octets bruts. `ByteReader` signale un dépassement au lieu de lire hors limites.

- [ ] **Étape 1 : Écrire les tests**
  - `roundtrip_each_message` : pour chacun des 4 types, `decode(encode(m)) == m`.
  - `decode_rejects_truncated` : pour chaque message encodé et chaque longueur `0..n-1`, `decode(prefix) == nullopt`.
  - `decode_rejects_unknown_type` : `decode({0xFF})` donne nullopt.
  - `decode_rejects_trailing_bytes` : un message valide suivi d'un octet supplémentaire donne nullopt.
  - `decode_rejects_bad_enum` : un `PlayerState` avec `anim = 7` donne nullopt.
  - `hello_name_is_nul_terminated` : un `Hello` dont le `name` contient 32 octets non nuls donne nullopt au décodage.
  - `decode_rejects_nonfinite_floats` : un `PlayerState` avec `x = NaN` ou `inf` donne nullopt.
- [ ] **Étape 2 :** `ctest --preset windows-debug -R protocol`. Résultat attendu : échec, car les symboles ne sont pas définis.
- [ ] **Étape 3 :** Implémenter `encode` et `decode`.
- [ ] **Étape 4 :** Relancer. Résultat attendu : tout passe.
- [ ] **Étape 5 :** Commit `feat(shared): wire protocol v1`.

### Tâche 3 : Logique de session du serveur (sans réseau)

**Files :** Create `server/include/wok/session.hpp`, `server/src/session.cpp`. Test : `server/tests/session_tests.cpp`.

**Interfaces :**
- Consumes : `wok::Message`, `encode` et `decode` (Tâche 2).
- Produces :
  ```cpp
  namespace wok {
  using ConnId = uint32_t;                      // handle de connexion attribué par le transport
  struct Outgoing { ConnId to; Message msg; };
  struct SessionConfig { uint32_t maxPlayers = 250; double timeoutSec = 10.0; std::unordered_set<uint64_t> bannedSteamIds; };
  class SessionManager {
  public:
    explicit SessionManager(SessionConfig);
    std::vector<Outgoing> onConnect(ConnId);
    std::vector<Outgoing> onPacket(ConnId, std::span<const uint8_t>, double nowSec);
    std::vector<Outgoing> onDisconnect(ConnId);
    std::vector<Outgoing> tick(double nowSec);              // timeouts
    std::vector<ConnId>   connectionsToClose() ;            // vidé à chaque appel
    size_t playerCount() const;
  };
  }
  ```
  Règles : un `Hello` est obligatoire avant tout autre message. Le serveur réécrit toujours `PlayerState.playerId` avec l'id de la connexion émettrice (un client ne peut pas usurper un autre id). Un `PlayerState` est relayé à **tous les autres** joueurs acceptés. Les `PlayerId` commencent à 1 et ne sont jamais réutilisés pendant la vie du processus. À l'acceptation, le nouveau joueur reçoit le dernier `PlayerState` connu de chaque joueur déjà présent.

- [ ] **Étape 1 : Écrire les tests**
  - `join_accepted_assigns_id` : Hello v1 renvoie `HelloAck{None, 1}`, et `playerCount() == 1`.
  - `join_rejected_when_full` : avec `maxPlayers = 2`, le 3e Hello reçoit `HelloAck{ServerFull, 0}` et sa connexion est dans `connectionsToClose()`.
  - `join_rejected_on_version_mismatch` : `protocolVersion = 2` donne `VersionMismatch`.
  - `join_rejected_when_banned` : un steamId banni donne `Banned`.
  - `state_relayed_to_others_not_sender` : avec A et B connectés, un `PlayerState` de A produit exactement un `Outgoing` vers B, avec `playerId == idA`.
  - `state_playerid_is_overwritten` : A envoie un `PlayerState` avec `playerId = 999` ; B le reçoit avec `idA`.
  - `state_before_hello_is_ignored` : aucun `Outgoing`.
  - `late_joiner_gets_existing_states` : A envoie sa position, puis B rejoint ; B reçoit `HelloAck` puis le `PlayerState` de A.
  - `disconnect_broadcasts_player_left` : A se déconnecte, B reçoit `PlayerLeft{idA}`.
  - `session_times_out_silent_player` : A est silencieux plus de 10 s ; `tick()` envoie `PlayerLeft{idA}` à B et ferme A.
  - `session_kicks_after_three_bad_packets` : 3 paquets non décodables mettent A dans `connectionsToClose()`.
- [ ] **Étape 2 :** Lancer les tests. Résultat attendu : échec.
- [ ] **Étape 3 :** Implémenter `SessionManager`.
- [ ] **Étape 4 :** Lancer les tests. Résultat attendu : tout passe.
- [ ] **Étape 5 :** Commit `feat(server): session manager`.

### Tâche 4 : Transport réseau, exécutable serveur et faux client

**Files :** Create `shared/include/wok/transport.hpp`, `shared/src/transport.cpp`, `server/src/main.cpp`, `tools/testclient/main.cpp`. Test : `shared/tests/transport_tests.cpp`.

**Interfaces :**
- Consumes : `SessionManager` (Tâche 3), `encode` et `decode` (Tâche 2).
- Produces :
  ```cpp
  namespace wok {
  bool netInit(); void netShutdown();           // GameNetworkingSockets_Init/Kill, appelés une fois par processus
  struct NetEvent { enum Kind { Connected, Disconnected, Packet } kind; ConnId conn; std::vector<uint8_t> data; };
  class NetServer { public: bool listen(uint16_t port); void send(ConnId, std::span<const uint8_t>, bool reliable);
                    void close(ConnId); std::vector<NetEvent> poll(); };
  class NetClient { public: bool connect(const char* host, uint16_t port); void send(std::span<const uint8_t>, bool reliable);
                    std::vector<NetEvent> poll(); bool connected() const; void disconnect(); };
  }
  ```
  `Hello`, `HelloAck` et `PlayerLeft` sont envoyés en mode fiable. `PlayerState` est envoyé en non fiable.
- `wok_server --port 27800 --max-players 250 [--ban-file bans.txt]` : boucle à 60 Hz qui fait `poll()`, puis `SessionManager`, puis `send`, et affiche connexions et déconnexions sur la console. Il s'arrête proprement sur Ctrl+C.
- `wok_testclient --host 127.0.0.1 --port 27800 --name Bot1 [--radius 10]` : envoie `Hello`, puis à 10 Hz un `PlayerState` qui décrit un cercle de rayon `radius` autour de l'origine. Il affiche les `PlayerState` reçus des autres.

- [ ] **Étape 1 : Écrire le test** `loopback_two_clients_see_each_other` : un `NetServer` sur un port libre (27900) piloté par un `SessionManager` dans le même thread de test, plus deux `NetClient`. Les deux envoient `Hello` puis un `PlayerState`. En moins de 3 s, chaque client a reçu le `PlayerState` de l'autre, avec le bon `playerId`.
- [ ] **Étape 2 :** Lancer le test. Résultat attendu : échec.
- [ ] **Étape 3 :** Implémenter le transport, `main.cpp` et le faux client.
- [ ] **Étape 4 :** Lancer le test. Résultat attendu : il passe. Ensuite, test manuel : `wok_server` plus deux `wok_testclient`. Chacun doit afficher les positions de l'autre, et quand on tue un client avec Ctrl+C, l'autre affiche `PlayerLeft` au plus tard 10 s après.
- [ ] **Étape 5 :** Commit `feat: GNS transport, dedicated server exe, test client`.

### Tâche 5 : DLL chargée par Kenshi (plugin OGRE) et installation

**Files :** Create `client/src/plugin.cpp`, `client/src/log.hpp`, `client/src/log.cpp`, `client/src/config.hpp`, `client/src/config.cpp`, `tools/install.ps1`.

**Interfaces :**
- Produces : les exports C `extern "C" __declspec(dllexport) void dllStartPlugin();` et `void dllStopPlugin();`. OGRE les appelle pour chaque `Plugin=` listé dans `Plugins_x64.cfg`. Ces fonctions ne doivent **rien** faire qui touche OGRE à part s'enregistrer. Le vrai démarrage a lieu à la première frame (Tâche 6).
- `wok::log(const char* fmt, ...)` écrit avec horodatage dans `<Kenshi>\WorldOfKenshi\wok_client.log`, de façon thread-safe.
- `wok::ClientConfig { std::string host = "127.0.0.1"; uint16_t port = 27800; std::string name = "Player"; }` et `ClientConfig loadConfig()` lisent `wok_client.ini`, en INI simple (`host=`, `port=`, `name=`). Si le fichier est absent, il est créé avec les valeurs par défaut.
- `tools/install.ps1 -KenshiDir <path> -BuildDir <path>` : sauvegarde `Plugins_x64.cfg` dans `D:\Backups\Kenshi-cfg-<horodatage>\`, copie `wok_client.dll` et ses DLL dépendantes (`GameNetworkingSockets.dll`, libs crypto/protobuf de vcpkg, `MinHook.x64.dll` si ce n'est pas en statique) dans le dossier Kenshi, et ajoute `Plugin=wok_client` s'il est absent. Le script est idempotent. `-Uninstall` retire la ligne et les DLL.
- La DLL est liée à la CRT **statique** (`/MT`) pour ne dépendre d'aucune redistribuable.

- [ ] **Étape 1 :** Implémenter et compiler.
- [ ] **Étape 2 :** Lancer `tools/install.ps1`, puis lancer Kenshi via Steam. Résultat attendu : `wok_client.log` contient `World of Kenshi client loaded (protocol 1)`, et le jeu atteint le menu principal normalement.
- [ ] **Étape 3 :** Lancer `install.ps1 -Uninstall`, puis Kenshi. Résultat attendu : le jeu est vanilla et aucun log n'est écrit.
- [ ] **Étape 4 :** Commit `feat(client): load as OGRE plugin + installer`.

> Si OGRE refuse le plugin (aucune ligne dans le log), utiliser comme repli un proxy `dinput8.dll` ou `version.dll`, selon ce que Kenshi charge réellement. Vérifier dans les imports de `kenshi_x64.exe` et des DLL OGRE. Documenter le choix dans `docs/re/README.md`.

### Tâche 6 : Hook de frame et scanner de signatures

**Files :** Create `client/src/ogre_abi.hpp`, `client/src/frame_hook.hpp`, `client/src/frame_hook.cpp`, `client/src/sigscan.hpp`, `client/src/sigscan.cpp`. Test : `client/tests/sigscan_tests.cpp`.

**Interfaces :**
- `ogre_abi.hpp` : déclaration minimale de `Ogre::FrameEvent { float timeSinceLastEvent; float timeSinceLastFrame; }` et d'une classe `Ogre::FrameListener` dont la vtable reproduit l'ordre de la version d'OGRE de Kenshi : `frameStarted`, `frameRenderingQueued`, `frameEnded`, destructeur virtuel. **Vérifier cet ordre avec REA** sur `OgreMain_x64.dll`, par exemple via la vtable `??_7FrameListener@Ogre@@6B@` ou un listener connu de Kenshi, et le noter dans `docs/re/ogre.md`. `Root::getSingleton` et `Root::addFrameListener` sont résolus à l'exécution avec `GetProcAddress` sur les noms décorés importés par Kenshi : `?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ` et `?addFrameListener@Root@Ogre@@QEAAXPEAVFrameListener@2@@Z`.
- `void wok::installFrameHook(std::function<void(float dt)> onFrame);` : à appeler depuis `dllStartPlugin`. Si `Root` n'existe pas encore, on réessaie au prochain appel possible, via un hook MinHook de secours sur `Root::renderOneFrame` si nécessaire. `onFrame` est appelé sur le thread du jeu, toutes les exceptions sont capturées et loggées.
- `sigscan.hpp` :
  ```cpp
  namespace wok {
  struct Signature { const char* name; const char* pattern; int offset = 0; enum Kind { Direct, RipRel32 } kind = Direct; };
  // pattern style "48 8B 05 ?? ?? ?? ?? 48 85 C0"
  const uint8_t* scan(std::span<const uint8_t> region, const char* pattern);       // nullptr si absent
  uintptr_t resolve(std::span<const uint8_t> region, uintptr_t regionBase, const Signature&); // 0 si absent, logge "signature not found: <name>"
  std::span<const uint8_t> moduleText(const char* moduleName);                     // section .text du module
  }
  ```
  `RipRel32` : l'adresse de la cible est `match + offset + 4 + int32 lu à (match + offset)`.

- [ ] **Étape 1 : Écrire les tests** (exécutés hors jeu, sur des buffers synthétiques)
  - `scan_finds_exact_pattern`
  - `scan_handles_wildcards`
  - `scan_returns_null_when_absent`
  - `scan_rejects_malformed_pattern` : `"4G ??"` renvoie nullptr sans planter.
  - `resolve_riprel32` : un buffer `48 8B 05 10 00 00 00`, avec offset 3 et base 0x1000, donne `0x1000 + 3 + 4 + 0x10`.
- [ ] **Étape 2 :** Lancer les tests. Résultat attendu : échec.
- [ ] **Étape 3 :** Implémenter.
- [ ] **Étape 4 :** Lancer les tests. Résultat attendu : ils passent. En jeu, avec `onFrame` qui logge le FPS une fois toutes les 5 s, le log montre une valeur cohérente.
- [ ] **Étape 5 :** Commit `feat(client): frame hook + signature scanner`.

### Tâche 7 : Reverse engineering avec REA — personnage local et création d'avatar

**Files :** Create `docs/re/game-world.md`, `docs/re/character.md` et `client/src/game/offsets.hpp`.

C'est une tâche de **découverte**. Le résultat attendu est une liste de signatures et d'offsets qui marchent, pas du code de gameplay. Outils : REA (`open_binary` sur `kenshi_x64.exe`, `search_strings`, `xrefs`, `procedure_pseudo_code`). Si besoin, le RTTI de MSVC 2010 est présent : chercher les chaînes `.?AV`.

Il faut trouver et documenter :
1. **Le singleton du monde de jeu**, avec la classe RTTI (pistes : `GameWorld`, `ou`) et sa signature `RipRel32`.
2. **Le personnage contrôlé par le joueur** : la liste des escouades du joueur, puis le personnage sélectionné ou le chef d'escouade. Il faut aussi un moyen de savoir si le jeu est en partie ou au menu.
3. **La position et l'orientation d'un personnage** : l'offset vers sa position monde (`Ogre::Vector3`) et vers son orientation (ou son `SceneNode`). Il faut aussi un indicateur de mouvement pour distinguer Idle, Walk et Run.
4. **La création d'un personnage** : la fonction qui fait apparaître un PNJ à une position donnée (fabrique de personnages ou d'escouades), avec sa signature et ses paramètres. Il faut aussi comment lui donner un ordre de déplacement (« aller à X,Y,Z ») et comment le détruire.
   - **Repli** si (4) dépasse **2 jours** de reverse : l'avatar devient une simple entité OGRE (`SceneManager::createEntity` avec un mesh de personnage Kenshi et une animation de marche). Attention à l'ABI : les fonctions OGRE qui prennent un `Ogre::String` reçoivent une structure de 40 octets compatible avec `std::string` de VC10, construite par nous dans `ogre_abi.hpp`.
5. Pour chaque élément : nom, signature, offset, et vérification en jeu (valeur loggée cohérente en déplaçant le personnage).

- [ ] **Étape 1 :** Faire les points 1 à 3 et les mettre dans `offsets.hpp` sous forme de `constexpr Signature`.
- [ ] **Étape 2 :** Vérifier en jeu avec un log de position à 1 Hz : les coordonnées changent quand on marche et restent fixes à l'arrêt.
- [ ] **Étape 3 :** Faire le point 4 (ou le repli) et le documenter.
- [ ] **Étape 4 :** Relire `docs/re/*.md` : on n'y trouve que des signatures, des offsets et des explications en français ou en anglais, **aucun pseudo-code recopié**.
- [ ] **Étape 5 :** Commit `docs(re): game world, character, spawn signatures`.

### Tâche 8 : Envoi de la position du joueur local

**Files :** Create `client/src/game/local_player.hpp`, `client/src/game/local_player.cpp`, `client/src/net_client.hpp`, `client/src/net_client.cpp`. Modify `client/src/plugin.cpp`.

**Interfaces :**
- Consumes : `offsets.hpp` (Tâche 7), `NetClient` (Tâche 4), `installFrameHook` (Tâche 6), `ClientConfig` (Tâche 5).
- Produces :
  ```cpp
  namespace wok {
  struct LocalSnapshot { float x, y, z, yaw; AnimState anim; };
  std::optional<LocalSnapshot> readLocalPlayer();   // nullopt si pas en jeu / pas de perso / signature manquante
  class ClientNet {   // thread réseau dédié ; échanges avec le thread du jeu via file protégée
  public:
    void start(const ClientConfig&);  void stop();
    void pushLocal(const LocalSnapshot&);            // thread jeu, à 10 Hz
    std::vector<Message> drainIncoming();            // thread jeu, chaque frame
    enum class Status { Disconnected, Connecting, Connected, Rejected } status() const;
    RejectReason rejectReason() const;
  };
  }
  ```
  Pour M1, la connexion est déclenchée par la touche **F9** en jeu, avec l'hôte et le port lus dans `wok_client.ini`. Un refus est loggé et affiché (Tâche 10).

- [ ] **Étape 1 :** Implémenter.
- [ ] **Étape 2 : Vérification manuelle**. Lancer `wok_server` et `wok_testclient --name Watcher`, puis Kenshi en jeu et appuyer sur F9. Le Watcher doit afficher les positions de Kenshi, et elles changent quand le personnage marche. Au menu principal, dans un écran de chargement, ou quand le personnage est mort, aucun `PlayerState` n'est envoyé et rien ne plante.
- [ ] **Étape 3 :** Couper `wok_server` pendant la partie. Le jeu continue, le log indique `disconnected`, et F9 permet de se reconnecter.
- [ ] **Étape 4 :** Commit `feat(client): send local player state`.

### Tâche 9 : Avatars des joueurs distants

**Files :** Create `client/src/game/avatars.hpp`, `client/src/game/avatars.cpp`, `client/src/interp.hpp`. Test : `client/tests/interp_tests.cpp`.

**Interfaces :**
- Consumes : les messages de `ClientNet::drainIncoming()` (Tâche 8) et la fonction de création ou le repli (Tâche 7).
- Produces :
  ```cpp
  namespace wok {
  // interp.hpp — pur, testable
  class Interpolator { public:
    void push(double recvTimeSec, const PlayerState&);
    std::optional<LocalSnapshot> sample(double nowSec) const;  // rendu à now - 0.100 s ; extrapole max 0.25 s puis fige
  };
  class AvatarManager { public:
    void onMessage(const Message&, double nowSec);   // PlayerState => crée/maj ; PlayerLeft => détruit
    void update(double nowSec);                      // pousse les positions interpolées dans le jeu
    void clear();                                    // déconnexion / retour au menu
  };
  }
  ```
- [ ] **Étape 1 : Écrire les tests de l'interpolateur**
  - `interp_midpoint` : avec des états à t=0 (x=0) et t=0.1 (x=10), `sample(0.15)` (rendu à 0.05) donne x=5.
  - `interp_single_sample_returns_it`
  - `interp_extrapolation_capped` : 1 s après le dernier paquet, la position est figée à la valeur extrapolée à +0.25 s.
  - `interp_out_of_order_ignored` : un paquet avec un `tick` plus ancien est ignoré.
  - `interp_yaw_wraps` : de 350° à 10°, le milieu donne 0°, pas 180°.
- [ ] **Étape 2 :** Lancer les tests. Résultat attendu : échec.
- [ ] **Étape 3 :** Implémenter `Interpolator` puis `AvatarManager`. L'avatar apparaît au premier `PlayerState`. Il reçoit un ordre de déplacement vers la position interpolée, ou il est placé directement si l'écart dépasse 20 m (téléportation). Il est détruit sur `PlayerLeft` et en revenant au menu.
- [ ] **Étape 4 :** Lancer les tests. Résultat attendu : ils passent. En jeu, avec `wok_testclient --radius 10` près de l'origine du serveur, on voit un personnage marcher en cercle avec l'animation de marche. Couper le testclient fait disparaître le personnage au plus tard 10 s après.
- [ ] **Étape 5 :** Commit `feat(client): remote player avatars with interpolation`.

### Tâche 10 : Interface minimale (statut et refus)

**Files :** Modify `client/src/plugin.cpp`. Create `client/src/ui.hpp` et `client/src/ui.cpp`.

Le spec dit qu'un bouton Multiplayer et une saisie d'IP suffisent pour M1. Pour réduire le risque d'ABI avec MyGUI, M1 se limite à :
- un **texte de statut en surimpression** (`World of Kenshi — Connecté (3 joueurs)` / `Connexion…` / `Refusé : serveur plein`), en overlay OGRE ou MyGUI, via le même shim de `std::string` VC10 ;
- la connexion par **F9** avec `wok_client.ini`.

Le vrai bouton « Multiplayer » dans le menu principal et la saisie d'IP sont reportés à M2. **À valider par Aeglen.**

Messages de refus (texte exact) :
- `ServerFull` → « Serveur plein »
- `VersionMismatch` → « Version du mod différente du serveur »
- `Banned` → « Vous êtes banni de ce serveur »
- `BadAuth` → « Authentification Steam refusée »

- [ ] **Étape 1 :** Implémenter.
- [ ] **Étape 2 :** Vérifier en jeu les états Connexion, Connecté, Déconnecté et Refusé. Pour obtenir un refus, lancer `wok_server --max-players 1` avec un testclient déjà connecté.
- [ ] **Étape 3 :** Commit `feat(client): connection status overlay`.

### Tâche 11 : Validation M1 de bout en bout

- [ ] **Étape 1 (une machine) :** Lancer `wok_server`, Kenshi avec le mod (F9) et un `wok_testclient`. Le testclient voit Kenshi bouger, et Kenshi voit le bot marcher.
- [ ] **Étape 2 (deux joueurs, le vrai M1) :** Faire tourner `wok_server` sur une machine accessible (port UDP 27800 ouvert). Deux PC avec Kenshi et le mod lancent chacun une partie, se placent au même endroit (par exemple Hub, nouvelle partie « Wanderer » des deux côtés) et appuient sur F9. **Chacun voit le personnage de l'autre marcher.** Note : le reste du monde (PNJ, etc.) n'est pas encore synchronisé, c'est normal pour M1.
- [ ] **Étape 3 :** Mettre à jour le README : comment compiler, installer, lancer le serveur, et ce qui marche ou non. Ajouter `docs/m1-demo.md` avec captures ou GIF.
- [ ] **Étape 4 :** Commit `docs: M1 demo`. Tag `v0.1.0-m1`.

---

## Hors de ce plan (M2 et suivants, voir le spec)

La vérification du ticket Steam, le vrai bouton Multiplayer, la liste des serveurs, le personnage dédié au serveur (création et chargement), la grille et le streaming de cases, les propriétaires de case avec succession par ping, la synchro des PNJ, du combat et de l'inventaire, la persistance SQLite, la resynchronisation par hash, et les instances headless.
