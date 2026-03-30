voxel.register_block({
    id = "test:hopper",
    on_construct = function(pos)
        local inv = voxel.get_inventory(pos)
        inv:set_size("input", 5)
        inv:set_size("output", 5)
    end,
    on_inventory_move = function(pos, from_list, from_index, to_list, to_index, count, player)
        test_move_from = from_list
        test_move_to = to_list
        test_move_count = count
    end,
})
