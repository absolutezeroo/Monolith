voxel.register_block({
    id = "test:target_block",
    on_projectile_hit = function(pos, projectile, hit_result)
        test_projectile_hit = true
    end,
})
