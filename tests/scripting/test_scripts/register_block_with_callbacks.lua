-- Test script: registers a block with placement and destruction callbacks.
-- Callbacks write to global tables so tests can verify they were called.

callback_log = {}

voxel.register_block({
    id = "test:callback_block",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 1.0,
    texture_indices = {1, 1, 1, 1, 1, 1},
    drop = "test:callback_block",

    on_construct = function(pos)
        table.insert(callback_log, "on_construct:" .. pos.x .. "," .. pos.y .. "," .. pos.z)
    end,

    on_destruct = function(pos)
        table.insert(callback_log, "on_destruct:" .. pos.x .. "," .. pos.y .. "," .. pos.z)
    end,

    after_place = function(pos, placer)
        table.insert(callback_log, "after_place:" .. pos.x .. "," .. pos.y .. "," .. pos.z)
    end,

    after_destruct = function(pos, old_block_id)
        table.insert(callback_log, "after_destruct:" .. pos.x .. "," .. pos.y .. "," .. pos.z)
    end,

    after_dig = function(pos, old_block_id, digger)
        table.insert(callback_log, "after_dig:" .. pos.x .. "," .. pos.y .. "," .. pos.z)
    end,
})
