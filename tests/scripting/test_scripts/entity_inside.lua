voxel.register_block({
    id = "test:cactus",
    on_entity_inside = function(pos, entity)
        local epos = entity:get_position()
        test_entity_pos_set = true
        test_entity_x = epos.x
        entity:damage(0.5)
    end,
})
