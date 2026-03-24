# Créer un voxel engine Minecraft-like en C++ moderne : guide technique complet

**Un moteur voxel performant en C++20/23 avec Vulkan repose sur quatre piliers : une architecture data-oriented avec ECS et pipeline de chunks asynchrone, un renderer GPU-driven à base d'indirect drawing, des conventions de code rigoureuses, et une méthodologie de projet structurée.** Ce rapport synthétise l'état de l'art technique en 2025-2026 pour chacun de ces piliers, avec des choix concrets de bibliothèques, des exemples de code, des benchmarks, et des références vers les meilleures ressources disponibles. L'objectif : fournir un plan d'exécution actionnable pour un développeur solo construisant un voxel engine moddable, network-ready, sur une base Vulkan.

---

## AXE 1 — Architecture engine, ECS et gestion du monde voxel

### La séparation en trois couches conditionne toute l'architecture

L'approche data-oriented design (DOD) surpasse systématiquement l'OOP classique pour les voxel engines. Le principe central : **Struct of Arrays plutôt qu'Array of Structs**, afin de maximiser la localité cache lorsqu'un système ne lit qu'un attribut à la fois. John Lin (voxely.net) démontre qu'aucun format voxel unique ne convient à tous les sous-systèmes — il faut un format canonique « brut » (flat array) puis des conversions vers des formats spécialisés par système (octree pour le raycasting, compressed pour la sérialisation, etc.).

L'architecture recommandée se divise en trois couches nettes. La **couche Core** contient les types mathématiques, allocateurs mémoire (pool/stack/arena), conteneurs, abstraction plateforme et job system. La **couche Engine** englobe le renderer Vulkan, le chunk manager, la physique, le host de scripting, l'audio et l'interface réseau. La **couche Game** est idéalement pilotée entièrement par le scripting Lua/Wren : gameplay, mods, définitions de contenu. Chaque système communique via des interfaces bien définies (événements, command queues, composants ECS partagés), ce qui prépare naturellement l'architecture réseau et le support de mods.

### EnTT pour le contrôle total, Flecs pour les fonctionnalités intégrées

**EnTT** (sparse-set, header-only C++17, ~30k stars GitHub) excelle pour un projet solo voulant garder le contrôle total : itération ultra-rapide sur les composants individuels, pas de scheduler imposé, fonctionne across DLL boundaries. **Flecs** (archetype-based, C/C++) offre en échange un scheduler multithreadé intégré, un langage de requêtes puissant, un support natif des hiérarchies/relations et un entity explorer UI — utilisé en production par Project Ascendant (vkguide.dev).

Le consensus critique : **ne pas stocker les données voxel des chunks dans l'ECS**. Les chunks doivent vivre dans un ChunkManager dédié avec leur propre stockage optimisé. L'ECS sert pour les entités de jeu (joueurs, mobs, items, projectiles) et les métadonnées de chunks (position, état, dirty flags). EnTT est recommandé comme point de départ pour un projet solo — sa simplicité compense l'absence de scheduler intégré qu'on remplacera par enkiTS.

### Stockage des chunks : flat array en runtime, palette compression en mémoire/disque

Le format de départ le plus simple et performant est le **flat array** :

```cpp
struct Chunk {
    static constexpr int SIZE = 16;
    uint16_t blocks[SIZE * SIZE * SIZE]; // 8 KB pour 16³
};
// Index: blocks[y * SIZE * SIZE + z * SIZE + x]
```

Nick McDonald (nickmcd.me) a mesuré que les flat arrays offrent un meshing plus rapide que les octrees malgré une consommation mémoire supérieure. Pour réduire cette empreinte, la **palette compression** (approche Minecraft 1.13+) est incontournable : chaque chunk maintient une palette locale d'indices vers les block IDs globaux, stockés dans un BitBuffer à bits-per-entry variable. Avec ≤2 types de blocs dans un chunk, on descend à **1 bit/voxel soit 512 octets par chunk 16³**. Le bits-per-entry croît dynamiquement (1→2→4→8…) à mesure que la palette s'enrichit.

Pour le disque, combiner RLE + LZ4 par-dessus la palette compression donne des ratios de **2x à 30x** (mesures de zeux.io, ingénieur Roblox). L'approche hybride de zeux.io pour la mémoire — rows non allouées stockant un seul byte pour les lignes uniformes — réduit encore la consommation pour les mondes avec beaucoup d'air.

### Binary greedy meshing : l'état de l'art à 74μs par chunk

Le meshing transforme les données voxel en géométrie renderable. Trois niveaux de sophistication existent :

Le **naive culling** (n'émettre que les faces entre voxels solides et air) génère ~30 000 quads par chunk pour ~500μs. Le **greedy meshing** classique (Mikola Lysenko, 0fps.net, 2012) fusionne les faces coplanaires adjacentes de même type en rectangles maximaux, réduisant à ~2 000-5 000 quads mais toujours en ~300-500μs. Le **binary greedy meshing** (cgerikj/binary-greedy-meshing) révolutionne les performances en utilisant des entiers 64 bits comme bitmasks pour traiter **64 faces simultanément** via des opérations bitwise.

Résultat : **50-200μs par chunk** en single-thread (moyenne 74μs, Ryzen 3800x), avec un format de vertex de seulement **8 octets par quad** (6-bit x,y,z + 6-bit width,height + 8-bit voxel type). Le rendu s'effectue via vertex pulling avec `MultiDrawIndirect` — un seul draw call pour tous les chunks. La version 2 est « plusieurs fois plus rapide » que la V1 grâce aux contributions d'Ethan Gore et Finding Fortune. Un port Rust mesure ~30x plus rapide que la référence block-mesh-rs.

### Le pipeline chunk asynchrone en 6 étapes

Chaque chunk traverse un pipeline strict :

**Generate** (worker thread) → **Populate** (worker thread, structures cross-boundary) → **Light** (worker thread, BFS flood-fill) → **Mesh** (worker thread, binary greedy) → **GPU Upload** (transfer queue Vulkan) → **Render** (main thread, indirect draw buffer).

Le job system **enkiTS** (dougbinks/enkiTS) est le choix recommandé : licence zlib, léger, C/C++11, utilisé en production dans Avoyd (voxel engine commercial). Il offre 5 niveaux de priorité, pinned tasks, zéro allocation pendant le scheduling, et support des dépendances. **Taskflow** est l'alternative pour les pipelines DAG complexes CPU+GPU mais plus lourd. Le pattern d'implémentation : queues concurrentes requête/résultat, avec `std::future` ou callback pour notifier le thread principal quand un chunk est prêt. Limiter les uploads à N chunks par frame pour maintenir le framerate.

### World generation : noise + splines + biomes

**FastNoiseLite** (Auburn/FastNoiseLite) est le choix par défaut : single-header, C/C++/GLSL, supporte Simplex, OpenSimplex2, Perlin, Value, Cellular, avec domain warping intégré. **FastNoise2** offre l'accélération SIMD (SSE/AVX) via un node-graph pour la génération en masse.

La clé de la génération terrain convaincante n'est pas le bruit brut mais les **spline curves** qui remappent les valeurs de bruit — c'est exactement ainsi que Minecraft crée des terrains distincts. L'approche en couches : bruit 2D basse fréquence pour l'élévation continentale, spline pour la distribution montagne/plaine, bruit fractal multi-octave pour le détail, bruit 3D pour caves et surplombs.

Le système de biomes utilise des cartes de bruit 2D pour **température**, **humidité**, **continentalness** et **érosion**, combinées via un diagramme de Whittaker (température × humidité → type de biome). Le blending entre biomes interpole les fonctions de hauteur aux frontières via moyenne pondérée par la distance.

### Physique voxel : AABB clipping par axe + DDA raycasting

La collision AABB style Minecraft ne permet jamais l'intersection : elle **clippe le delta de mouvement** contre les AABBs des voxels environnants. L'ordre de résolution est critique : Y d'abord (gravité), puis X, puis Z. Pour chaque axe, on étend l'AABB du joueur par le vecteur vélocité, on collecte les blocs candidats, et on clippe le delta contre chacun.

Le raycasting utilise l'algorithme **DDA 3D** (Digital Differential Analyzer) adapté aux grilles voxel : à chaque pas, on avance le long de l'axe ayant la plus petite `sideDist` (distance au prochain bord de cellule), garantissant un parcours exact de tous les voxels traversés par le rayon.

### Registries : IDs numériques en runtime, IDs string pour la sérialisation

```cpp
class BlockRegistry {
    std::vector<BlockDefinition> blocks;           // indexé par uint16_t ID
    std::unordered_map<std::string, uint16_t> nameToId; // "base:stone" → 42
};
```

Les IDs numériques (`uint16_t`, 65 535 types) servent au stockage en chunk. Les string IDs (`"namespace:name"`) servent à la sérialisation et au modding. La palette compression dans les chunks fait le lien entre indices locaux et IDs globaux du registre. Le registre est peuplé au démarrage par les data files + scripts Lua/Wren des mods.

### Préparation réseau : command pattern + simulation tick-based

Sans implémenter le réseau immédiatement, quatre patterns garantissent la network-readiness. Le **Command Pattern** : chaque action de jeu est un objet sérialisable (`PlaceBlock`, `MovePlayer`, `UseItem`) passé par un pipeline — pas de mutation directe de l'état. La **simulation tick-based** : boucle de jeu à timestep fixe (20 ticks/sec comme Minecraft), toute la logique en pas discrets. La **séparation Game State / Render State** : l'état de jeu est la « vérité », l'état de rendu interpole entre les ticks. Et la **communication par messages** : même en singleplayer, tous les changements d'état passent par un système d'événements.

L'architecture Quake-style (même le singleplayer tourne en client+serveur interne) force cette séparation propre dès le départ — c'est exactement ce que fait Luanti. Les ressources clés : la série en 4 parties de Gabriel Gambetta (gabrielgambetta.com) et les articles de Glenn Fiedler (gafferongames.com).

### Scripting : Lua via sol2 + LuaJIT comme choix principal

**sol2** (ThePhD/sol2) est le binding Lua le plus rapide et le plus ergonomique en C++ : header-only, MIT, supporte Lua 5.1-5.4 + LuaJIT + MoonJIT, API moderne (lambdas, containers, user types). LuaJIT offre des performances proches du C pour les boucles chaudes. Le sandboxing est trivial en Lua (whitelist des fonctions disponibles) et le hot-reloading se fait par ré-exécution des scripts.

**Wren** (Bob Nystrom, auteur de « Crafting Interpreters ») est une alternative class-based plus familière pour les développeurs C++/Java, avec fibers intégrées et VM de seulement 4 000 semicolons. Mais son écosystème est bien plus petit, sans JIT, et les bindings C++ (Wren++) sont moins matures que sol2. La recommandation : **Lua/sol2/LuaJIT comme primaire**, Wren optionnel comme secondaire.

L'API de modding suit le modèle Luanti : hooks d'événements (`on_block_placed`, `on_entity_damaged`), API de registre (`register_block()`, `register_item()`), API monde (`get_block(pos)`, `set_block(pos, id)`, `raycast()`), sandbox désactivant `os`, `io`, `debug` et limitant l'accès filesystem au dossier du mod.

### Projets open-source de référence à étudier

**Luanti** (ex-Minetest) : C++ + Lua via LuaJIT, architecture client-serveur même en singleplayer, milliers de mods — la référence absolue pour le design d'API de modding. **Project Ascendant** (vkguide.dev) : C++ + Vulkan + Flecs, indirect rendering GPU-driven, gigabuffer 400 MB — la référence pour le pipeline de rendu voxel moderne. **Craft** (fogleman) : ~3 500 lignes de C, montre la simplicité possible du cœur. **ClassiCube** : C99, cross-platform extrême (Nintendo DS). **Veloren** : Rust, ECS, voxel RPG multijoueur — excellente référence d'architecture à grande échelle. **VoxelCore** (MihailRis) : C++ moderne + EnTT + Lua, bon exemple de structure propre. **Cubiquity** : Sparse Voxel DAG expérimental, compression extrême.

---

## AXE 2 — Renderer Vulkan GPU-driven pour voxels

### L'architecture Ascendant comme modèle de référence

Le renderer voxel le plus documenté publiquement est celui de **Project Ascendant** (Victor Blanco, vkguide.dev). Ses choix architecturaux définissent l'état de l'art. Chunks de **8×8×8** pour un culling fin (Minecraft utilise 16×16×16, Vintage Story 32×32×32). **Deferred rendering** obligatoire pour unifier l'éclairage quand on a 5 systèmes de géométrie différents (meshes voxels, sprites far-draw, végétation, meshes GLTF, etc.). Le **gigabuffer pattern** : un seul VkBuffer de 400 MB alloué au démarrage, sub-alloué via VMA Virtual Allocation — offsets 32 bits au lieu de pointeurs BDA 64 bits, transferts simplifiés, logique indirect draw directe.

Les deux goulots d'étranglement GPU identifiés sont la **mémoire pour les meshes générés** (croissance O(n³) avec la distance de rendu) et la **densité géométrique** à distance (1 pixel = 1 voxel). Les solutions : compression agressive des vertex, LOD pour le terrain distant, switch vers du sprite raycasting pour les draws lointains.

### Vulkan 1.3 + vk-bootstrap + VMA + volk : le stack d'initialisation

**vk-bootstrap** élimine ~500 lignes de boilerplate pour Instance/PhysicalDevice/Device/Swapchain via un builder pattern C++17. Configuration recommandée ciblant Vulkan 1.3 avec dynamic rendering (skip des renderpasses/framebuffers), synchronization2, buffer device address, et descriptor indexing (bindless textures).

**VMA** (Vulkan Memory Allocator) gère la sub-allocation mémoire GPU : `VMA_MEMORY_USAGE_AUTO` laisse VMA choisir le type optimal, staging buffers via `HOST_ACCESS_SEQUENTIAL_WRITE_BIT | MAPPED_BIT`, support ReBAR avec fallback automatique. La fonctionnalité `VmaVirtualBlock` sert de bookkeeping CPU-side pour le sub-alloc dans le gigabuffer. **volk** charge tous les function pointers Vulkan core + extensions sans lier directement à vulkan-1.dll, avec intégration directe dans VMA via `vmaImportVulkanFunctionsFromVolk()`.

Ordre d'initialisation : volk → vk-bootstrap (Instance/Device) → VMA allocator.

### Indirect rendering : un seul draw call pour le monde entier

Le pattern gold standard : un compute shader de culling itère tous les chunks, teste la visibilité (frustum), atomically appends les survivants dans un indirect buffer, puis `vkCmdDrawIndexedIndirectCount` (Vulkan 1.2) laisse le GPU décider **à la fois** des paramètres de draw et du nombre de draws. Tous les chunk meshes résident dans le gigabuffer unique. Un index buffer partagé pour les quads (0,1,2,2,3,0 répété) élimine les index buffers per-chunk. Les données per-draw (position monde du chunk, material ID) passent via SSBO indexé par `gl_DrawID` ou `firstInstance`.

```hlsl
// Compute shader de culling simplifié (style Ascendant)
void mainPassCull(uint3 threadId: SV_DispatchThreadID) {
    uint idx = threadId.x;
    if (idx < sceneData[0].chunkCount && chunkInfo[idx].drawcount > 0) {
        if (IsVisible(chunkInfo[idx], sceneData[0])) {
            uint drawIdx = atomicAdd(drawCount[0], 1);
            drawCommands[drawIdx].indexCount = chunkInfo[idx].drawcount * 6;
            drawCommands[drawIdx].vertexOffset = int32_t(chunkInfo[idx].index) * 4;
            drawCommands[drawIdx].instanceCount = 1;
            drawCommands[drawIdx].firstInstance = drawIdx;
        }
    }
}
```

### Formats de vertex ultra-compacts : 8 octets par quad

La compression des vertex est critique pour les voxels. Le format **binary greedy meshing** pack un quad entier en **8 octets** : 6-bit x,y,z + 6-bit width,height + 8-bit voxel type, lu par vertex pulling depuis un SSBO. Le format **Exile** utilise 8 octets par vertex (uvec2) : `[x:8][z:8][u:8][v:8]` + `[y:12][textureID:12][ao0:2][ao1:2][ao2:2][ao3:2]` — 12 bits pour le texture ID supportent 4 096 textures de blocs. Le format **Ascendant** pour les far-draw compresse un bloc visible entier en **4 octets** : `drawflags:4 | pos:12 | type:16`.

Le vertex pulling remplace les VBO/VAO traditionnels : le shader lit directement depuis un storage buffer via `gl_VertexID`, permettant des formats arbitrairement compacts. En Vulkan, on utilise les Buffer Device Addresses (BDA) ou des storage buffers pour cet accès.

### Ambient occlusion baked : 4 niveaux par vertex de face

La technique standard pour l'AO voxel, documentée par 0fps.net, calcule 4 valeurs d'AO possibles par vertex basées sur 3 blocs adjacents (2 côtés + 1 coin) :

```cpp
int ao_value(bool side1, bool corner, bool side2) {
    if (side1 && side2) return 0;  // occlusion maximale
    return 3 - side1 - side2 - corner;
}
// Courbe AO: [0.75, 0.825, 0.9, 1.0]
```

Détail critique d'implémentation : les **4 valeurs AO doivent être encodées dans CHAQUE vertex** du quad, puis le fragment shader interpole bilinéairement en utilisant les coordonnées UV. L'interpolation barycentrique par défaut du GPU (par triangle) donne des résultats incorrects pour un quad. De plus, quand les valeurs AO sont anisotropes, la diagonale de subdivision du quad doit être choisie pour minimiser les artefacts : flip quand `abs(ao[0]-ao[3]) > abs(ao[1]-ao[2])`.

### Tommo's cave culling : le culling d'occlusion spécifique aux voxels

L'algorithme de Tommaso Checchi (Minecraft) en deux parties élimine **50-99% de la géométrie**. La première partie pré-calcule un **graphe de connectivité** par sous-chunk 16³ : un flood fill à travers les blocs non-opaques enregistre quelles faces du chunk sont visibles depuis quelles autres faces — seulement 15 paires possibles (C(6,2)), stockables en bitmask. La seconde partie effectue un **BFS de visibilité** depuis le chunk caméra : on ne traverse vers un voisin que si le graphe de connectivité confirme que la face d'entrée peut voir la face de sortie, et seulement dans la direction s'éloignant de la caméra (N·V < 0). Ce BFS unique fournit frustum culling, tri en profondeur, scheduling de rebuild ET culling de visibilité.

Pour du GPU-driven occlusion culling complémentaire (Hierarchical Z-Buffer) : le pattern two-pass de Nanite/UE5 rend les objets visibles au frame précédent, construit un HZB (mip chain de profondeur max par texel via compute shader), puis teste tous les objets contre ce HZB.

### LOD : POP buffers pour des transitions sans couture

Trois approches principales pour le LOD voxel distant. Les **POP Buffers** (0fps.net) arrondissent les positions de vertex à des puissances de 2 inférieures par niveau de LOD, avec geomorphing dans le vertex shader pour des transitions seamless sans skirts. Les **clipmaps** (Voxel Farm / Procedural World) : anneaux concentriques autour du viewer, chaque anneau doublant la taille voxel, fonctionnent en 3D (pas seulement heightmap). L'approche **Ascendant** : meshes lissés SurfaceNets en near-field, sprite raycasting per-block (ray/box intersection dans le pixel shader) en far-field.

### Texture arrays, pas texture atlas

Les **texture arrays** (`VK_IMAGE_VIEW_TYPE_2D_ARRAY`) sont fortement recommandées pour Vulkan : pas de bleeding d'atlas, mipmaps par layer sans contamination cross-texture, coordonnées UV normalisées 0-1 par layer, single bind / single draw call. Chaque texture de bloc = une layer, sampling via `texture(sampler2DArray, vec3(u, v, layerIndex))`. Vulkan supporte typiquement plus de 2 048 layers. Les atlas restent pertinents uniquement pour les plateformes limitées.

### Ressources d'apprentissage Vulkan essentielles

**vkguide.dev** (Victor Blanco) : tutoriel project-based ciblant Vulkan 1.3, GPU-driven rendering, articles Ascendant. **vulkan-tutorial.com** : fondamentaux exhaustifs. **Sascha Willems' Vulkan Examples** : 80+ exemples standalone (indirect drawing, texture arrays, compute, mesh shaders). **« Writing an Efficient Vulkan Renderer »** (zeux.io, Arseny Kapoulkine) : guide de performance complet. **« How I Learned Vulkan »** (Elias Daler, edw.is) : abstraction GfxDevice en ~714 lignes, vertex pulling via BDA. **Exile Voxel Pipeline** (thenumb.at) : pipeline complet avec code shader. **fschoenberger.dev** : blog step-by-step d'un voxel engine C++20/Vulkan.

---

## AXE 3 — Conventions C++, style guide et outillage

### Conventions de nommage pour un game engine

Le standard le plus répandu pour les engines custom combine PascalCase et camelCase, distinct du snake_case de la STL pour marquer clairement le code moteur :

| Élément | Convention | Exemple |
|---------|-----------|---------|
| Classes/Structs | PascalCase | `ChunkManager`, `VoxelWorld` |
| Méthodes/Fonctions | camelCase | `getChunkAt()`, `generateMesh()` |
| Variables membres | `m_` + camelCase | `m_chunkSize`, `m_renderDistance` |
| Variables locales | camelCase | `blockIndex`, `localPos` |
| Constantes/constexpr | SCREAMING_SNAKE | `MAX_CHUNK_SIZE`, `BLOCK_AIR` |
| Namespaces | lowercase/snake_case | `voxel::core`, `rendering` |
| Fichiers | PascalCase (classe) | `ChunkManager.h` / `ChunkManager.cpp` |
| Interfaces | Préfixe `I` | `IRenderable`, `ISerializable` |
| Booléens | Préfixe `is`/`has`/`should` | `m_isLoaded`, `hasCollision()` |

### C++20/23 : ce qu'il faut adopter, ce qu'il faut éviter

L'analyse de Jeremy Ong pour les grandes codebases de jeu donne un verdict clair par feature. **À adopter sans hésiter** : `concepts` (remplacent SFINAE), designated initializers, expansions `constexpr`/`consteval`, `constinit`, `std::span`, three-way comparison, `[[likely]]`/`[[unlikely]]`. **Avec précaution** : `std::format` (temps de compilation élevés — garder fmtlib), ranges (compile times lourds, sélectivement en code non-hot-path), coroutines (bon pour le chargement d'assets async, pas encore mûr pour le frame-critical). **À éviter pour l'instant** : les **modules C++20** dont le tooling reste immature across compilers et CMake.

En C++23, les features les plus pertinentes sont **`std::expected<T,E>`** (error handling sans exceptions avec chaînage monadique), **`std::mdspan`** (vues multidimensionnelles parfaites pour les grilles voxel : `std::mdspan<Block, std::extents<int, 16, 16, 16>>`), **`std::flat_map`/`flat_set`** (conteneurs triés cache-friendly), **multidimensional `operator[]`** (syntax `chunk[x, y, z]`), et **`std::unreachable()`**.

### Error handling : pas d'exceptions, std::expected + assertions

Le consensus industriel pour les game engines est quasiment unanime : **les exceptions sont désactivées**. Elles augmentent la taille du binaire, dégradent le cache d'instructions, et rendent le control flow imprévisible en code temps réel. La stratégie recommandée en trois tiers :

```cpp
// Erreurs de programmeur → assertions (debug only)
VX_ASSERT(index < CHUNK_SIZE, "Block index out of bounds");

// Erreurs attendues → std::expected<T, Error>
enum class EngineError { FileNotFound, InvalidFormat, ShaderCompileError };
template<typename T> using Result = std::expected<T, EngineError>;

Result<Shader> loadShader(std::string_view path) {
    auto source = readFile(path);
    if (!source) return std::unexpected(EngineError::FileNotFound);
    return Shader{compiledProgram};
}

// Chaînage monadique C++23
auto pipeline = loadShader("vert.glsl")
    .and_then([](Shader s) { return linkProgram(s); })
    .or_else([](EngineError e) { log::error("Pipeline failed: {}", e); });

// Erreurs fatales → log + crash dump + std::abort()
```

### Structure de projet modulaire

```
VoxelEngine/
├── CMakeLists.txt                 # Root CMake
├── CMakePresets.json              # Build presets
├── vcpkg.json                     # Dépendances
├── .clang-format / .clang-tidy
├── cmake/                         # Modules CMake helper
│   ├── CompilerWarnings.cmake
│   └── Sanitizers.cmake
├── engine/                        # Bibliothèque moteur
│   ├── CMakeLists.txt
│   ├── include/voxel/             # Headers publics, namespacés
│   │   ├── core/   (Types.h, Assert.h, Log.h, Result.h)
│   │   ├── math/   (Vec3.h, AABB.h, Noise.h)
│   │   ├── world/  (Chunk.h, ChunkManager.h, Block.h, WorldGenerator.h)
│   │   ├── renderer/ (Renderer.h, Mesh.h, Shader.h, Camera.h)
│   │   ├── ecs/    (Entity.h, Component.h, System.h)
│   │   ├── physics/ (Collision.h)
│   │   └── scripting/ (ScriptEngine.h)
│   └── src/                       # Implémentation privée (miroir)
├── game/                          # Exécutable jeu
├── tests/                         # Tests unitaires (Catch2)
├── assets/                        # Shaders, textures, configs
│   ├── shaders/ / textures/ / scripts/
├── docs/                          # Doxygen
└── tools/                         # Asset compiler, etc.
```

Le moteur est une target library CMake ; le jeu linke contre elle. Les headers publics dans `include/voxel/` sont séparés de l'implémentation dans `src/`, avec une structure miroir.

### CMake moderne : targets, precompiled headers, presets

```cmake
cmake_minimum_required(VERSION 3.25)
project(VoxelEngine VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # Pour clangd/clang-tidy

add_library(VoxelEngine STATIC)
target_include_directories(VoxelEngine
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VoxelEngine
    PUBLIC  glm::glm spdlog::spdlog EnTT::EnTT
    PRIVATE glfw Vulkan::Vulkan)
target_precompile_headers(VoxelEngine PRIVATE
    <vector> <array> <string> <memory> <cstdint>
    <unordered_map> <expected> <glm/glm.hpp> <spdlog/spdlog.h>)
```

Les principes clés : utiliser des **targets** (pas des variables globales) avec propagation PUBLIC/PRIVATE/INTERFACE, `CMAKE_EXPORT_COMPILE_COMMANDS` pour le tooling, **precompiled headers** pour réduire significativement le temps de compilation, et **CMakePresets.json** pour des configurations reproductibles.

**vcpkg** est recommandé sur Conan pour un projet solo : intégration plus simple avec CMake via toolchain file, excellent support Windows, manifest mode avec `vcpkg.json`. Conan 2 reste pertinent pour les builds cross-compilation complexes ou les setups non-CMake.

### Configuration .clang-format recommandée

```yaml
Language: Cpp
BasedOnStyle: Microsoft
Standard: c++20
ColumnLimit: 120
IndentWidth: 4
UseTab: Never
BreakBeforeBraces: Allman
PointerAlignment: Left              # int* ptr
DerivePointerAlignment: false
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
NamespaceIndentation: None
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^"voxel/'              # Headers moteur en premier
    Priority: 1
  - Regex: '^<(glm|vulkan|SDL|imgui|spdlog|fmt|entt|glfw)'
    Priority: 2                     # Third-party
  - Regex: '^<'
    Priority: 3                     # System/STL
BinPackArguments: false
BinPackParameters: false
BreakConstructorInitializers: BeforeComma
Cpp11BracedListStyle: true
```

### Testing : Catch2 v3 pour l'ergonomie

Catch2 est préféré à Google Test pour un projet indie : setup plus simple, sections inline éliminant les classes fixtures, noms de test libres en strings, BDD natif (GIVEN/WHEN/THEN). Google Test + GMock reste pertinent si le mocking est intensif.

```cpp
TEST_CASE("Chunk block access", "[world][chunk]") {
    voxel::Chunk chunk;
    SECTION("Default blocks are air") {
        REQUIRE(chunk.getBlock(0, 0, 0) == voxel::BlockType::Air);
    }
    SECTION("Setting and getting a block") {
        chunk.setBlock(5, 10, 3, voxel::BlockType::Stone);
        REQUIRE(chunk.getBlock(5, 10, 3) == voxel::BlockType::Stone);
    }
}
```

### Documentation Doxygen style Javadoc

Style `/** */` avec commandes `@brief`, `@param`, `@return`, `@note`, `@see`. Placer `@brief` et `@param` dans les **headers** (déclarations), `@details` dans les **sources** (définitions). Activer `JAVADOC_AUTOBRIEF` pour que la première phrase soit automatiquement le brief. Générer des graphes d'appels/héritage avec DOT.

---

## AXE 4 — BMAD Method : méthodologie et roadmap de projet

### Un framework agile AI-driven pour structurer le développement

**BMAD** (Breakthrough Method for Agile AI-Driven Development) est un **framework open-source** (MIT, 35.4k stars GitHub) qui transforme le « vibe coding » en workflow structuré piloté par des agents IA spécialisés. Sa philosophie centrale : **la documentation est la source de vérité, pas le code** — le code est un dérivé downstream des artefacts de planification (PRD, architecture, user stories). Version actuelle : **v6.0.0-Beta.8** (février 2026), installable via `npx bmad-method install`.

### Les quatre phases séquentielles de BMAD

**Phase 1 — Analyse** (optionnelle) : brainstorming guidé par l'agent Mary (Analyst), recherche technique/marché, production d'un `product-brief.md` capturant la vision stratégique.

**Phase 2 — Planning** : l'agent John (PM) crée un `PRD.md` complet avec requirements fonctionnels et non-fonctionnels, l'agent Sally produit un `ux-spec.md` si applicable.

**Phase 3 — Solutioning** : l'agent Winston (Architect) produit un `architecture.md` avec ADRs (Architecture Decision Records), John découpe le travail en epics et stories, puis un gate check (`bmad-check-implementation-readiness`) valide que tout est prêt pour l'implémentation (PASS/CONCERNS/FAIL).

**Phase 4 — Implémentation** : l'agent Bob (Scrum Master) initialise le sprint tracking (`sprint-status.yaml`), prépare chaque story, l'agent Amelia (Developer) implémente story par story avec code review intégrée, `bmad-correct-course` gère les changements mid-sprint, et `bmad-retrospective` capture les leçons après chaque epic.

### Le module Game Dev Studio est conçu pour les engines custom

Le module **BMGD** (Game Dev Studio) adapte BMAD spécifiquement au développement de jeux, avec support explicite des **custom engines** au-delà d'Unity/Unreal/Godot. Il ajoute des workflows pour le Game Design Document (`/bmgd-gdd`), le narrative design, le prototypage rapide (`/bmgd-quick-dev`), et des agents spécialisés (Game Architect, Game Developer) avec connaissances engine-specific.

### Application concrète à un voxel engine C++

L'artefact critique est le **`project-context.md`** — le « constitution » du projet qui documente le tech stack, les patterns, et les règles d'implémentation pour que tous les agents IA suivent les conventions. Pour un voxel engine, il contiendrait : C++23, CMake, Vulkan, ECS (EnTT), scripting (Lua/sol2), conventions de nommage, patterns mémoire, dépendances.

La chaîne d'artefacts **EST** la roadmap :

- `product-brief.md` → Vision et scope (le « pourquoi »)
- `PRD.md` → Requirements avec epics définis (le « quoi »)  
- `architecture.md` → Blueprint technique avec ADRs (le « comment »)
- Fichiers epic/story → Découpage détaillé du travail
- `sprint-status.yaml` → Tracking et séquençage

### Roadmap suggérée en epics pour le voxel engine

En appliquant le découpage BMAD au projet voxel engine, les epics naturels seraient :

- **Epic 1 — Foundation** : Core layer (types, logging, math, job system enkiTS), CMake setup, vcpkg, CI
- **Epic 2 — Vulkan Bootstrap** : vk-bootstrap + VMA + volk, window, triangle de base, gigabuffer allocation
- **Epic 3 — Voxel World Core** : Chunk storage (flat array + palette), block registry, world coordinate system
- **Epic 4 — Terrain Generation** : FastNoiseLite integration, heightmap terrain, biome système basique
- **Epic 5 — Meshing Pipeline** : Binary greedy meshing, vertex format compact, GPU upload via transfer queue
- **Epic 6 — Rendering Pipeline** : Indirect drawing, compute culling, deferred rendering, texture arrays, AO baked
- **Epic 7 — Player Interaction** : Camera, input, AABB collision, DDA raycasting, block place/break
- **Epic 8 — Lighting** : Flood-fill sky/block light, deferred lighting pass
- **Epic 9 — Scripting** : Lua/sol2/LuaJIT intégration, block/item registry via Lua, sandboxing
- **Epic 10 — Polish & LOD** : Tommo culling, LOD (POP buffers ou clipmaps), frustum culling GPU, SSAO

Chaque epic produit un livrable fonctionnel testable, conformément au principe BMAD de stories implémentables individuellement avec critères d'acceptance clairs.

---

## Stack technique recommandée : synthèse décisionnelle

| Composant | Choix principal | Alternative |
|-----------|----------------|-------------|
| Langage | C++20, features C++23 sélectives | — |
| Renderer | Vulkan 1.3, dynamic rendering | — |
| ECS | EnTT (léger, solo-friendly) | Flecs (scheduler intégré) |
| Job system | enkiTS (prouvé voxel) | Taskflow (DAG complexes) |
| Chunk storage | Flat array + palette compression | Hybrid row-compressed |
| Meshing | Binary greedy meshing (~74μs/chunk) | Greedy meshing classique |
| Noise | FastNoiseLite | FastNoise2 (SIMD) |
| Scripting | Lua via sol2 + LuaJIT | Wren secondaire |
| Maths | GLM | — |
| Fenêtrage | GLFW ou SDL3 | — |
| Build | CMake 3.25+ + vcpkg | Conan 2 |
| Tests | Catch2 v3 | Google Test + GMock |
| Logging | spdlog | — |
| UI debug | Dear ImGui | — |
| Méthodologie | BMAD + module BMGD | — |

## Conclusion : les trois insights clés qui changent la donne

Premièrement, le **binary greedy meshing** transforme le meshing de goulot d'étranglement en opération quasi-gratuite — 74μs par chunk moyen permet le remeshing de dizaines de chunks par frame sans impact visible. C'est la technique qui rend viable un voxel engine ambitieux sur du hardware grand public.

Deuxièmement, le pattern **gigabuffer + indirect draw + compute culling** élimine la complexité CPU-side du rendu : un seul buffer de 400 MB, un compute shader de culling, un seul draw call indirect pour le monde entier. L'overhead driver passe de « des centaines de draw calls à gérer » à « essentiellement zéro ». Ce pattern, documenté en détail par Project Ascendant, est directement implémentable.

Troisièmement, préparer le réseau via le **command pattern + simulation tick-based** ne coûte quasiment rien en complexité initiale mais change fondamentalement la maintenabilité future. Chaque action de jeu en objet sérialisable, une boucle de jeu à pas fixe de 20 ticks/sec, et une séparation état de jeu / état de rendu — trois patterns qui rendent l'ajout ultérieur du multijoueur incrémental plutôt que chirurgical.