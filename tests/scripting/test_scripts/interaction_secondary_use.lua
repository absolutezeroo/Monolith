voxel.register_block({
    id = "test:wand_block",
    on_secondary_use = function(itemstack, user, pointed_thing)
        test_secondary_use_fired = true
        test_secondary_use_user = user
        return itemstack
    end,
})
