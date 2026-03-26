# Luanti vs VoxelForge — Architecture Gap Analysis

Analysé depuis le code source Luanti (github.com/minetest/minetest, HEAD 2026-03-26).

## Systèmes couverts (VoxelForge V1 = Luanti)

| Système Luanti | VoxelForge | Notes |
|---|---|---|
| MapNode storage (content_t + param1 + param2) | ChunkSection uint16_t + Block States (Story 3.4) | ✅ Equivalent |
| NodeDef / ContentFeatures | BlockDefinition + BlockRegistry | ✅ Equivalent |
| NodeBox (custom shapes) | Story 5.4 Non-Cubic Models | ✅ Equivalent |
| NDT_NORMAL / NDT_GLASSLIKE / NDT_ALLFACES | Opaque + Cutout + Translucent (Story 6.7) | ✅ Equivalent |
| Palette / color | Block Tinting (Story 5.5 + 6.8) | ✅ Equivalent |
| Light propagation (block + sky, BFS) | Epic 8 (Stories 8.0–8.4) | ✅ Equivalent |
| Lua scripting (sol2/LuaJIT sandbox) | Epic 9 (11 stories, 62 callbacks) | ✅ Supérieur |
| ABM / LBM / Node timers | Story 9.4 | ✅ Equivalent |
| Chunk loading / spiral / spatial hash | Story 3.5 ChunkManager | ✅ Equivalent |
| Palette compression + LZ4 | Story 3.6 + 3.7 | ✅ Equivalent |
| Collision AABB per-axis | Story 7.2 | ✅ Equivalent |
| DDA raycasting | Story 7.4 | ✅ Equivalent |
| Camera + FPS controls | Story 2.6 + 7.3 | ✅ Equivalent |
| Command pattern / event bus | Story 7.1 | ✅ Supérieur (Luanti n'a pas ça proprement) |
| Fixed timestep game loop | Story 2.1 | ✅ Equivalent |
| Debug overlay | Story 2.6 | ✅ Equivalent (ImGui vs Irrlicht HUD) |

## Systèmes couverts mais MIEUX chez VoxelForge

| Système | Luanti | VoxelForge | Avantage |
|---|---|---|---|
| Renderer | OpenGL 2.1+ via IrrlichtMt, hundreds of draw calls | Vulkan 1.3, GPU-driven, 1 indirect draw call | VoxelForge ×100 |
| Meshing | Custom greedy mesher, single-threaded | Binary greedy + enkiTS multithreaded | VoxelForge ×10-30 |
| Vertex format | ~32 bytes/vertex, VBO per mapblock | 8 bytes/quad, gigabuffer | VoxelForge ×4 VRAM |
| Culling | Frustum only (basic) | Frustum + Cave + HiZ (Epic 10) | VoxelForge ×10 |
| LOD | Aucun | POP buffers (Epic 10) | VoxelForge only |
| AO | Basic per-vertex | Per-vertex baked + SSAO | VoxelForge meilleur |
| C++ standard | C++17 (legacy patterns) | C++20/23, std::expected, concepts | VoxelForge plus propre |
| ECS | Aucun (listes custom) | EnTT | VoxelForge plus extensible |

## SYSTÈMES MANQUANTS — Existant chez Luanti, absent de VoxelForge

### CRITIQUE — Doit être dans V1 ou modifie les epics existants

**1. Node "waving" (feuilles, plantes qui bougent)**
Luanti: `u8 waving` (0=none, 1=leaves, 2=plants, 3=liquid surface)
Le vertex shader applique un déplacement sinusoïdal basé sur la position + le temps.
Impact: change le vertex shader (Story 6.2). 3 bits dans le quad format suffisent.
→ Ajouter dans BlockDefinition + quad format + chunk.vert

**2. "climbable" property (échelles, lianes)**
Luanti: `bool climbable` — quand le joueur est dans un bloc climbable, il peut monter/descendre avec espace/shift.
Impact: change la physique joueur (Story 7.3 PlayerController).
→ Ajouter dans BlockDefinition + PlayerController::update()

**3. "move_resistance" (toiles d'araignée, miel, eau)**
Luanti: `u8 move_resistance` (0-7) — ralentit le joueur à l'intérieur du bloc.
Impact: change la physique joueur (Story 7.3).
→ Ajouter dans BlockDefinition + PlayerController::update()

**4. "drowning" (étouffement sous l'eau/dans le sable)**
Luanti: `u8 drowning` — réduit l'air du joueur, dégâts quand à 0.
Impact: nécessite un système de souffle (breath) + UI, change Story 7.3.
→ Post-V1, mais BlockDefinition doit avoir le champ maintenant

**5. "damage_per_second" (cactus, lave, feu)**
Luanti: `u32 damage_per_second` — dégâts passifs quand le joueur est dans le bloc.
Déjà couvert par `on_entity_inside` (Story 9.6) mais le champ statique est plus simple pour les cas basiques.
→ Ajouter dans BlockDefinition, check dans PlayerController

**6. "buildable_to" (herbe haute remplacée par un bloc posé)**
Luanti: `bool buildable_to` — le placement d'un bloc REMPLACE celui-ci au lieu d'échouer.
Déjà partiellement couvert par `can_be_replaced` callback (Story 9.2) mais le flag statique est nécessaire pour le behavior par défaut.
→ Ajouter dans BlockDefinition

**7. "floodable" (les liquides remplacent ce bloc)**
Luanti: `bool floodable` — l'eau qui coule remplace ce bloc (torches, fleurs).
Couvert par `on_flood` callback (Story 9.2) + flag statique.
→ Ajouter dans BlockDefinition

**8. Node groups (crumbly, cracky, snappy, choppy...)**
Luanti: `ItemGroupList groups` — chaque bloc appartient à des groupes avec un rating.
Les outils ont des `ToolGroupCap` qui définissent le temps de minage par groupe+rating.
ESSENTIEL pour un vrai système de minage (pioche mine la pierre vite mais pas le bois).
Impact: change BlockDefinition + nécessite un ToolCapabilities système.
→ Ajouter en Story 9.2 : `groups = { cracky = 3, stone = 1 }` dans le register_block

### IMPORTANT — Post-V1 mais doit être prévu architecturalement

**9. Tool capabilities system**
Luanti: `ToolCapabilities` struct — dig time par groupe, max level, uses (durabilité), damage groups.
`ToolGroupCap` = { times = {[1]=3.0, [2]=1.5, [3]=0.7}, maxlevel=3, uses=30 }
C'est LE système qui transforme le block-breaking d'un "instant break" en un vrai mining avec progression.
→ Epic 13 (Inventaire + Crafting) ou Epic séparé "Tool System"

**10. Crafting system**
Luanti: `CraftDef` — shaped, shapeless, cooking, fuel. Entièrement Lua-driven.
`core.register_craft({ type="shaped", output="base:stick 4", recipe={{"base:wood"}} })`
→ Epic 14 (Crafting), mais l'API Lua est prête grâce à Story 9.2

**11. Liquid/fluid system**
Luanti: `LiquidType` enum (source/flowing), `liquid_viscosity`, `liquid_range`, `liquid_renewable`, `ReflowScan`.
L'eau coule, se propage, crée de nouvelles sources entre deux sources.
→ Epic 15 (Fluids), prévoir les champs dans BlockDefinition maintenant

**12. Sound system**
Luanti: `SoundSpec` par node — `sound_footstep`, `sound_dig`, `sound_dug`.
Sons positionnels via OpenAL, sons de pas qui changent par type de bloc.
→ Epic 12 (Audio), prévoir les champs dans BlockDefinition maintenant

**13. Animated textures**
Luanti: `TileAnimationParams` — vertical frames ou sheet 2D, durée par frame.
Eau, lave, feu, portails — les textures sont des sprite sheets animés.
Impact: le TextureArray (Story 6.5) doit supporter les frames animées.
→ Ajouter un champ `animation` dans la définition de texture du register_block

**14. Post-effect color (overlay sous l'eau, dans la lave)**
Luanti: `video::SColor post_effect_color` — quand la caméra est DANS le bloc, un overlay coloré couvre l'écran.
Bleu sous l'eau, rouge dans la lave, vert dans le slime.
→ 1 champ dans BlockDefinition + fullscreen quad shader. Petit travail.

**15. Item entities (objets lâchés au sol)**
Luanti: `__builtin:item` — quand un bloc est cassé, un item entity spawne, tombe avec gravité, le joueur le ramasse en marchant dessus.
En Lua pur chez Luanti (builtin/game/item_entity.lua).
→ Nécessite le système d'entités ECS, post-V1

**16. Falling nodes (sable, gravier)**
Luanti: `__builtin:falling_node` — les blocs avec le groupe "falling_node" tombent quand le support en-dessous est retiré.
En Lua pur chez Luanti (builtin/game/falling.lua), utilise les entités.
→ Peut être implémenté via ABM (Story 9.4) + check on_neighbor_changed

**17. HUD system (programmable)**
Luanti: `HudElement` — image, text, statbar, inventory, waypoint, compass, minimap.
Les mods ajoutent/modifient des éléments HUD via API Lua.
→ Post-V1, mais l'overlay ImGui actuel est suffisant pour V1

**18. Formspec (GUI déclaratif)**
Luanti: système de GUI déclaratif pour les inventaires, crafting, menus.
`"size[8,9]inventory[current_player;main;0,5;8,4;]list[context;main;0,0;8,3;]"`
→ Post-V1, nécessaire pour inventaire/crafting

**19. Clouds**
Luanti: rendu de nuages 3D voxelisés, animés, paramétrables par Lua.
→ Post-V1, cosmétique

**20. Sky system (soleil, lune, étoiles)**
Luanti: `SkyboxParams`, `SunParams`, `MoonParams`, `StarParams` — tout paramétrable par Lua.
Soleil/lune avec textures, étoiles procédurales, couleurs de ciel par cycle jour/nuit.
→ Post-V1 mais le cycle jour/nuit (Story 8.4) doit prévoir l'interface

**21. Wield mesh (item tenu en main en 3D)**
Luanti: `WieldMesh` — le bloc/outil tenu par le joueur est rendu en 3D dans le coin de l'écran.
→ Post-V1, cosmétique mais très attendu visuellement

**22. Minimap**
Luanti: top-down minimap + radar mode, toggleable, scriptable via Lua.
→ Post-V1

**23. Connected nodeboxes (fences)**
Luanti: `NODEBOX_CONNECTED` — les nodeboxes changent de forme selon les voisins connectés.
Partiellement couvert par `update_shape` callback (Story 9.5) mais les connected boxes sont un cas spécial important.
→ Story 5.4 doit supporter ça dans le ModelMesher

## Recommandation : quoi changer MAINTENANT

### Champs à ajouter dans BlockDefinition (Story 3.3) — coût: 10 minutes

```cpp
struct BlockDefinition {
    // ...existing...
    
    // Physics properties (utilisés par PlayerController, Story 7.3)
    bool isClimbable = false;           // Échelles, lianes
    uint8_t moveResistance = 0;         // 0-7, toiles d'araignée, miel
    uint32_t damagePerSecond = 0;       // Cactus, lave
    uint8_t drowning = 0;              // Air consommé/tick dans ce bloc
    bool isBuildableTo = false;        // Herbe remplacée par placement
    bool isFloodable = false;          // Eau remplace ce bloc
    
    // Visual properties (utilisés par le mesher + shader)
    uint8_t waving = 0;                // 0=none, 1=leaves, 2=plants, 3=liquid
    video::SColor postEffectColor{0};  // Overlay quand caméra dedans
    
    // Tool/mining groups (utilisés par le tool system, Epic futur)
    std::unordered_map<std::string, int> groups;  // "cracky"=3, "stone"=1
    
    // Sound (stub pour Epic 12)
    std::string soundFootstep;
    std::string soundDig;
    std::string soundDug;
    
    // Liquid (stub pour Epic 15)
    LiquidType liquidType = LiquidType::None;
    uint8_t liquidViscosity = 0;
    uint8_t liquidRange = 8;
    std::string liquidAlternativeFlowing;
    std::string liquidAlternativeSource;
    
    // Animation (stub pour post-V1)
    // TileAnimationParams animation; // uncomment when needed
};
```

### Stories existantes à modifier — coût: quelques lignes chacune

- **Story 5.4 (Non-cubic models)**: ajouter support `NODEBOX_CONNECTED` pour les fences
- **Story 6.2 (chunk.vert)**: ajouter waving vertex animation (3 bits dans quad format)
- **Story 7.3 (Player movement)**: checker `isClimbable`, `moveResistance`, `damagePerSecond`
- **Story 9.2 (Lua register)**: exposer `groups`, `climbable`, `move_resistance`, `buildable_to`, `floodable`, `waving`, `post_effect_color`, `sounds`
