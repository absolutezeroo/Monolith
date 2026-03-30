test_lbm_fired = false
test_lbm_count = 0

voxel.register_block({
    id = "test:torch_old",
})

voxel.register_lbm({
    label = "Upgrade old torches test",
    nodenames = { "test:torch_old" },
    run_at_every_load = false,
    action = function(pos, node, dtime_s)
        test_lbm_fired = true
        test_lbm_count = test_lbm_count + 1
    end,
})
