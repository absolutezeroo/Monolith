voxel.register_block({
    id = "test:sign",
    on_construct = function(pos)
        local meta = voxel.get_meta(pos)
        meta:set_string("text", "Hello")
        meta:set_int("line_count", 4)
        meta:set_float("scale", 1.5)
        test_meta_constructed = true
    end,
})
