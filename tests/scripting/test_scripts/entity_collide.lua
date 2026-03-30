voxel.register_block({
    id = "test:bumper",
    on_entity_collide = function(pos, entity, facing, velocity, is_impact)
        test_collide_facing = facing
        test_collide_is_impact = is_impact
        test_collide_vel_y = velocity.y
    end,
})
