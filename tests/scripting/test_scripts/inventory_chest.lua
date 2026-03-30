voxel.register_block({
    id = "test:chest",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("main", 27)
        test_chest_constructed = true
    end,
    allow_inventory_put = function(pos, listname, index, stack, player)
        return stack:get_count()
    end,
    on_inventory_put = function(pos, listname, index, stack, player)
        test_put_fired = true
        test_put_item = stack:get_name()
    end,
})
