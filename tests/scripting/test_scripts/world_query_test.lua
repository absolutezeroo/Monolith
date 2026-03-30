-- world_query_test.lua — Integration test for WorldQueryAPI
-- Exercises: block registration, set_block, find_blocks_in_area, count_blocks_in_area

-- Register a test block
voxel.register_block({
    id = "test:test_brick",
    solid = true,
    groups = { building = 1 }
})

-- Place a 5x5x5 cube of test_brick blocks
test_cube_count = 0
for x = 0, 4 do
    for y = 0, 4 do
        for z = 0, 4 do
            local result = voxel.set_block({x=x, y=y, z=z}, "test:test_brick")
            if result then
                test_cube_count = test_cube_count + 1
            end
        end
    end
end

-- Search the area for test_brick blocks
local positions = voxel.find_blocks_in_area(
    {x=0, y=0, z=0},
    {x=4, y=4, z=4},
    "test:test_brick"
)

test_cube_search_count = 0
if positions then
    for _, pos in ipairs(positions) do
        test_cube_search_count = test_cube_search_count + 1
    end
end

-- Verify count shortcut matches
test_cube_count_shortcut = voxel.count_blocks_in_area(
    {x=0, y=0, z=0},
    {x=4, y=4, z=4},
    "test:test_brick"
) or 0
