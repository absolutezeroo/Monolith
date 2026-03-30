voxel.register_block({
    id = "test:locked_chest",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("main", 9)
    end,
    allow_inventory_put = function(pos, listname, index, stack, player)
        return 0 -- Deny all
    end,
    allow_inventory_take = function(pos, listname, index, stack, player)
        return 0 -- Deny all
    end,
})
