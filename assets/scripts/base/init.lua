-- assets/scripts/base/init.lua
-- Base block registrations for VoxelForge
-- Migrated from blocks.json — all 29 blocks

voxel.register_block({
    id = "base:stone",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 1.5,
    texture_indices = {1, 1, 1, 1, 1, 1},
    drop = "base:cobblestone",
    groups = {cracky = 3, stone = 1},
})

voxel.register_block({
    id = "base:dirt",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.5,
    texture_indices = {2, 2, 2, 2, 2, 2},
    drop = "base:dirt",
    groups = {crumbly = 3},
})

voxel.register_block({
    id = "base:grass_block",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.6,
    texture_indices = {4, 4, 3, 2, 4, 4},
    tint_index = 1,
    drop = "base:dirt",
    groups = {crumbly = 3},
})

voxel.register_block({
    id = "base:sand",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.5,
    texture_indices = {5, 5, 5, 5, 5, 5},
    drop = "base:sand",
    falling = true,
    groups = {crumbly = 3, falling_node = 1},
})

voxel.register_block({
    id = "base:water",
    solid = false,
    transparent = true,
    has_collision = false,
    light_filter = 2,
    hardness = 100.0,
    texture_indices = {6, 6, 6, 6, 6, 6},
    tint_index = 3,
    drop = "",
    render_type = "translucent",
    replaceable = true,
    move_resistance = 3,
    drowning = 1,
    post_effect_color = 0x80000044,
    liquid = {
        type = "source",
        viscosity = 1,
        range = 8,
        renewable = true,
    },
})

voxel.register_block({
    id = "base:oak_log",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 2.0,
    texture_indices = {8, 8, 7, 7, 8, 8},
    drop = "base:oak_log",
    groups = {choppy = 2, wood = 1},
})

voxel.register_block({
    id = "base:oak_leaves",
    solid = true,
    transparent = true,
    has_collision = true,
    light_filter = 1,
    hardness = 0.2,
    texture_indices = {9, 9, 9, 9, 9, 9},
    tint_index = 2,
    drop = "",
    render_type = "cutout",
    waving = 1,
    floodable = true,
    groups = {choppy = 3, leafdecay = 3},
})

voxel.register_block({
    id = "base:glass",
    solid = true,
    transparent = true,
    has_collision = true,
    hardness = 0.3,
    texture_indices = {10, 10, 10, 10, 10, 10},
    drop = "",
    render_type = "translucent",
})

voxel.register_block({
    id = "base:glowstone",
    solid = true,
    has_collision = true,
    light_emission = 15,
    light_filter = 15,
    hardness = 0.3,
    texture_indices = {11, 11, 11, 11, 11, 11},
    drop = "base:glowstone",
    groups = {cracky = 3},
})

voxel.register_block({
    id = "base:bedrock",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = -1.0,
    texture_indices = {13, 13, 13, 13, 13, 13},
    drop = "",
})

voxel.register_block({
    id = "base:sandstone",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.8,
    texture_indices = {14, 14, 14, 14, 14, 14},
    drop = "base:sandstone",
    groups = {cracky = 3, stone = 1},
})

voxel.register_block({
    id = "base:snow_block",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.2,
    texture_indices = {15, 15, 15, 15, 15, 15},
    drop = "base:snow_block",
    groups = {crumbly = 3},
})

voxel.register_block({
    id = "base:torch",
    solid = false,
    transparent = true,
    has_collision = false,
    light_emission = 14,
    hardness = 0.0,
    texture_indices = {12, 12, 12, 12, 12, 12},
    drop = "base:torch",
    floodable = true,
    model_type = "torch",
    groups = {dig_immediate = 3},
})

voxel.register_block({
    id = "base:birch_log",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 2.0,
    texture_indices = {17, 17, 16, 16, 17, 17},
    drop = "base:birch_log",
    groups = {choppy = 2, wood = 1},
})

voxel.register_block({
    id = "base:birch_leaves",
    solid = true,
    transparent = true,
    has_collision = true,
    light_filter = 1,
    hardness = 0.2,
    texture_indices = {18, 18, 18, 18, 18, 18},
    tint_index = 2,
    drop = "",
    render_type = "cutout",
    waving = 1,
    floodable = true,
    groups = {choppy = 3, leafdecay = 3},
})

voxel.register_block({
    id = "base:spruce_log",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 2.0,
    texture_indices = {20, 20, 19, 19, 20, 20},
    drop = "base:spruce_log",
    groups = {choppy = 2, wood = 1},
})

voxel.register_block({
    id = "base:spruce_leaves",
    solid = true,
    transparent = true,
    has_collision = true,
    light_filter = 1,
    hardness = 0.2,
    texture_indices = {21, 21, 21, 21, 21, 21},
    tint_index = 2,
    drop = "",
    render_type = "cutout",
    waving = 1,
    floodable = true,
    groups = {choppy = 3, leafdecay = 3},
})

voxel.register_block({
    id = "base:jungle_log",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 2.0,
    texture_indices = {23, 23, 22, 22, 23, 23},
    drop = "base:jungle_log",
    groups = {choppy = 2, wood = 1},
})

voxel.register_block({
    id = "base:jungle_leaves",
    solid = true,
    transparent = true,
    has_collision = true,
    light_filter = 1,
    hardness = 0.2,
    texture_indices = {24, 24, 24, 24, 24, 24},
    tint_index = 2,
    drop = "",
    render_type = "cutout",
    waving = 1,
    floodable = true,
    groups = {choppy = 3, leafdecay = 3},
})

voxel.register_block({
    id = "base:cactus",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.4,
    texture_indices = {26, 26, 25, 25, 26, 26},
    drop = "base:cactus",
    damage_per_second = 1,
    groups = {choppy = 3},
})

voxel.register_block({
    id = "base:tall_grass",
    solid = false,
    transparent = true,
    has_collision = false,
    hardness = 0.0,
    texture_indices = {27, 27, 27, 27, 27, 27},
    tint_index = 1,
    drop = "",
    render_type = "cutout",
    model_type = "cross",
    waving = 1,
    floodable = true,
    buildable_to = true,
    replaceable = true,
    groups = {dig_immediate = 3, flora = 1},
})

voxel.register_block({
    id = "base:flower_red",
    solid = false,
    transparent = true,
    has_collision = false,
    hardness = 0.0,
    texture_indices = {28, 28, 28, 28, 28, 28},
    drop = "base:flower_red",
    render_type = "cutout",
    model_type = "cross",
    waving = 1,
    floodable = true,
    groups = {dig_immediate = 3, flora = 1},
})

voxel.register_block({
    id = "base:flower_yellow",
    solid = false,
    transparent = true,
    has_collision = false,
    hardness = 0.0,
    texture_indices = {29, 29, 29, 29, 29, 29},
    drop = "base:flower_yellow",
    render_type = "cutout",
    model_type = "cross",
    waving = 1,
    floodable = true,
    groups = {dig_immediate = 3, flora = 1},
})

voxel.register_block({
    id = "base:dead_bush",
    solid = false,
    transparent = true,
    has_collision = false,
    hardness = 0.0,
    texture_indices = {30, 30, 30, 30, 30, 30},
    drop = "",
    render_type = "cutout",
    model_type = "cross",
    floodable = true,
    buildable_to = true,
    replaceable = true,
    groups = {dig_immediate = 3},
})

voxel.register_block({
    id = "base:snow_layer",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 0.1,
    texture_indices = {31, 31, 31, 31, 31, 31},
    drop = "",
    model_type = "slab",
    floodable = true,
    buildable_to = true,
    replaceable = true,
    groups = {crumbly = 3},
})

voxel.register_block({
    id = "base:coal_ore",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 3.0,
    texture_indices = {32, 32, 32, 32, 32, 32},
    drop = "base:coal_ore",
    groups = {cracky = 2, stone = 1},
})

voxel.register_block({
    id = "base:iron_ore",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 3.0,
    texture_indices = {33, 33, 33, 33, 33, 33},
    drop = "base:iron_ore",
    groups = {cracky = 2, stone = 1},
})

voxel.register_block({
    id = "base:gold_ore",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 3.0,
    texture_indices = {34, 34, 34, 34, 34, 34},
    drop = "base:gold_ore",
    groups = {cracky = 2, stone = 1},
})

voxel.register_block({
    id = "base:diamond_ore",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 3.0,
    texture_indices = {35, 35, 35, 35, 35, 35},
    drop = "base:diamond_ore",
    groups = {cracky = 1, stone = 1},
})
