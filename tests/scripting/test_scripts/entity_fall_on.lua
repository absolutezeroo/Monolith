voxel.register_block({
    id = "test:slime_block",
    on_entity_fall_on = function(pos, entity, fall_distance)
        test_fall_distance = fall_distance
        local vel = entity:get_velocity()
        entity:set_velocity({x = vel.x, y = -vel.y * 0.8, z = vel.z})
        return 0.0
    end,
})
