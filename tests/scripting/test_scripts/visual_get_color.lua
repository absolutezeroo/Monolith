-- Test: get_color returns a tint value based on position
voxel.register_block({
    id = "test:tinted_grass",
    solid = true,
    texture_indices = {0,0,0,0,0,0},

    get_color = function(pos)
        -- Return different colors based on Y coordinate
        if pos.y > 64 then
            return 0x00FF00  -- bright green at high altitude
        end
        return 0x006600  -- dark green below
    end,
})
