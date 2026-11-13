# World of Kenshi — Document de conception

*Date : 2026-11-12 · Auteur : Aeglen (vision), Claude (rédaction)*
*Source de la vision : `docs/vision-flowchart.pdf`*

## 1. Intention

Transformer Kenshi en MMO sur serveurs dédiés. On garde le même jeu, le même monde et les mêmes règles, mais partagés entre de nombreux joueurs. Chaque joueur se connecte à un serveur avec un personnage propre à ce serveur. Il y construit sa vie, sa base et sa faction. Ce que fait un joueur est répliqué chez tous les autres joueurs concernés.

Le projet part de zéro. Les mods multijoueur existants ont été essayés et jugés insuffisants. Notre code (le mod et le serveur) est open source sur GitHub, avec l'objectif de postuler au programme OSS de Claude.

## 2. Objectifs

- Serveurs dédiés visant **jusqu'à 250 joueurs** connectés.
- **Kenshi de base** : aucun changement de gameplay obligatoire. Le mod ajoute le réseau, pas un nouveau jeu.
- **Personnage dédié au serveur** : impossible d'importer un personnage ou une sauvegarde solo.
- **Support des mods (essentiel pour la communauté)** : un serveur peut tourner avec des mods de données (FCS). Voir §7.6. Hors M1.
- **Cadavres permanents** : un corps (PNJ ou joueur) ne disparaît jamais tout seul. Il reste dans le monde, pillable, et survit aux redémarrages du serveur. Le serveur stocke chaque cadavre (position, inventaire, équipement, race/apparence par stringID FCS) dans sa persistance. Hors M1.
- **Persistance façon Rust** : un joueur déconnecté laisse son personnage, ses recrues et sa base dans le monde, vulnérables. Un mécanisme de protection sera conçu plus tard.
- **Réplication** des actions de chaque joueur (déplacement, combat, construction, inventaire, factions) vers les autres clients concernés.
- Code propre et publiable : README, licence, build reproductible.

## 3. Non-objectifs (pour l'instant)

- Protection hors-ligne des bases : à concevoir plus tard.
- Nouveau contenu de gameplay.
- Mods qui injectent du code (plugins DLL type RE_Kenshi) : pas supportés au début.
- Anti-triche avancé : la V1 se limite à des vérifications de plausibilité.

## 4. Anti-objectifs (ce serait un échec même si ça marche)

- Publier du code de Kenshi, qu'il soit décompilé ou recopié, ou des fichiers du jeu. **Seul notre code est publié.**
- Un monde qui diverge entre les joueurs : deux joueurs dans la même zone ne doivent jamais voir deux réalités différentes de façon durable.
- Dépendre de KenshiLib, RE_Kenshi ou des projets « Kenshi Online » existants.

## 5. Contraintes

- Kenshi (Steam, `kenshi_x64.exe`, moteur OGRE, Windows x64). Le jeu de base n'a pas d'API réseau.
- Les développeurs tolèrent le modding. On injecte une DLL et on pose des hooks sur des fonctions du jeu trouvées par reverse engineering (avec l'outil REA).
- **Règle de reverse** : REA sert à *comprendre* (adresses, structures, comportements). On *écrit* notre propre code. Les adresses et définitions de structures dans le repo sont acceptées, comme dans SKSE.
- Kenshi simule mal un grand nombre d'entités actives. Aucun client ne peut simuler un monde de 250 joueurs.
- Chaque joueur, et chaque hébergeur d'instances headless, possède sa propre copie Steam du jeu.

## 6. Parcours joueur (tiré du schéma de la vision)

1. Le joueur lance Kenshi. Le mod vérifie via Steam que le joueur possède le jeu. Sinon, il ne peut pas jouer.
2. Le mod est activé. Le menu principal affiche un bouton **« Multiplayer »**.
3. Ce bouton ouvre la **liste des serveurs**. Le joueur choisit un serveur.
4. Le mod demande au serveur : « Je peux rejoindre ? »
   - **Non** : on affiche la raison (mod ou version manquante, serveur plein, banni).
   - **Oui** : authentification avec un ticket Steam, puis chargement ou création du personnage serveur.
5. Le serveur envoie l'état du monde (les cases autour du joueur). Le mod l'applique localement.
6. Le client renvoie un résumé (hash) de l'état appliqué : « Est-ce que c'est exact ? »
   - **Oui** : le joueur joue.
   - **Non** : on tente une resynchronisation. Après plusieurs échecs, le joueur est kické avec « Connection Timeout ».

## 7. Architecture (décisions prises)

### 7.1 Composants
- **Mod client** : DLL C++ injectée dans `kenshi_x64.exe`. Elle pose les hooks sur le jeu, gère l'interface Multiplayer et la liste des serveurs, et applique l'état reçu.
- **Serveur dédié** : programme C++ autonome, qui n'est pas Kenshi. Il fait autorité sur l'état persistant : comptes, personnages, inventaires, bâtiments, factions, positions. Il découpe le monde en grille, désigne les propriétaires et relaie les messages. Il doit tourner sur Windows et sur Linux.
- **Code partagé** : protocole, sérialisation et définitions de la grille, communs au client et au serveur.

### 7.2 Monde en grille et streaming
- La carte est découpée en **cases (chunks)** de taille fixe.
- Chaque client reçoit seulement les cases autour de lui (rayon d'intérêt) et les mises à jour de ces cases.

### 7.3 Propriétaire de case
- Chaque case occupée a **un propriétaire**. C'est son instance de Kenshi qui simule la case : IA des PNJ, combat, dégâts, besoins.
- Le propriétaire envoie l'état de simulation au serveur, qui le redistribue aux autres joueurs de la case. Ces joueurs affichent cet état sans simuler eux-mêmes les PNJ.
- Chaque joueur contrôle ses propres personnages. Ses actions passent par le serveur et sont résolues par le propriétaire.
- **Succession par ping** : pour chaque case, le serveur tient à jour un **classement des clients présents trié par meilleur ping**. Le premier est le propriétaire, les suivants sont ses successeurs, dans l'ordre. Quand le propriétaire part, le suivant prend le relais immédiatement à partir du dernier état connu, sans élection.
- **Case vide** : sans joueur, la case est figée. Le serveur stocke son état et rien ne bouge jusqu'au retour de quelqu'un.
- Le serveur vérifie la plausibilité des données envoyées : vitesse de déplacement, dégâts compatibles avec les stats, objets qui apparaissent de nulle part. En cas d'écart, il resynchronise.

### 7.4 Évolution prévue : instances headless
Le rôle de « propriétaire » est **générique** dans le protocole. Plus tard, le serveur pourra lancer des instances de Kenshi **sans affichage** (rendu, son et interface coupés par notre DLL) qui deviennent propriétaires des cases. Ça enlève l'autorité aux clients, ce qui règle l'essentiel de la triche, et ça permet aux cases vides de continuer à vivre. Le prix : une machine Windows puissante et une copie du jeu côté hébergeur. **Hors V1**, mais l'architecture ne doit pas l'empêcher.

### 7.5 Choix techniques (choix de Claude, non contestés par Aeglen)
- C++20 et CMake, pour le client comme pour le serveur.
- Réseau : **GameNetworkingSockets** (Valve, open source).
- Authentification : tickets d'authentification Steam. Kenshi charge déjà `steam_api64.dll`.
- Licence : MIT.
- Repo GitHub public.

### 7.6 Mods
Les mods font vivre la communauté : un serveur moddé doit être un cas normal, pas une exception.
- **Le serveur impose la liste de mods et l'ordre de chargement** (comme les serveurs ARK/Rust moddés). Tous les clients d'un serveur ont exactement le même contenu, sinon le monde diverge.
- **Vérification à la connexion** : le client envoie sa liste (identifiant + empreinte de chaque fichier `.mod`, dans l'ordre). Différence → refus avec la raison « Mods manquants / différents : X, Y » (le cas « dependency » du schéma de la vision).
- **Mods Steam Workshop identifiés par leur ID Workshop** ; plus tard, téléchargement automatique au moment de rejoindre un serveur. Les mods hors Workshop sont identifiés par nom + empreinte.
- **Cible : les mods de données** (FCS : objets, races, factions, bâtiments, armures…). Ils passent par le moteur normal, donc la réplication les couvre comme le contenu de base.
- **Plugins DLL** (code injecté) : non supportés au début, car on ne peut pas garantir qu'ils restent synchronisés.
- **Règle d'architecture, dès maintenant** : le protocole désigne tout contenu (objet, race, faction, bâtiment…) par son **identifiant de données FCS** (chaîne `stringID`), jamais par un index numérique codé en dur ou un pointeur. Un objet ajouté par un mod circule ainsi comme un objet du jeu de base.

## 8. Premier jalon (M1) : « ça marche »

**Deux joueurs se connectent au même serveur et se voient marcher en jeu.**

Ce que M1 implique au minimum :
- La DLL est injectée et chargée dans Kenshi.
- Un serveur minimal accepte deux connexions. Le ticket Steam peut être simplifié ou désactivé en mode dev.
- Chaque client envoie la position et l'orientation de son personnage.
- Chaque client fait apparaître et bouge un personnage représentant l'autre joueur, avec une animation de marche.
- Un bouton Multiplayer et une saisie d'IP suffisent. La vraie liste des serveurs viendra plus tard.

Hors M1 : propriétaires de case, PNJ, combat, inventaire, persistance, grille.

## 9. Laissé au constructeur

- La méthode d'injection : proxy DLL (par exemple un `dinput8.dll` ou une DLL que Kenshi charge déjà) ou un launcher. Il faut préférer ce qui marche avec un lancement via Steam.
- La librairie de hooks (MinHook ou équivalent).
- La taille des cases, le rayon d'intérêt et la fréquence d'envoi.
- Le format de sérialisation.
- Le stockage de persistance côté serveur (SQLite par défaut).
- La structure du repo et la CI.

## 10. Questions ouvertes (à trancher plus tard)

- Le mécanisme de protection des joueurs et des bases hors-ligne.
- Le comportement exact d'une case qui « dégèle » après une longue absence : rattrapage du temps écoulé ou pas.
- Comment répliquer les factions et les relations entre factions de joueurs.
- Cadavres permanents à grande échelle : impact sur les perfs (Kenshi n'a pas été conçu pour des milliers de corps) — faut-il un moyen de les faire disparaître (enterrer, brûler, découper) ou une limite côté admin ?
- Téléchargement automatique des mods Workshop manquants au moment de rejoindre (mécanique exacte).
- La découverte des serveurs : liste maîtresse ou listing Steam.
