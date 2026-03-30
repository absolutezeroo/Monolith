#pragma once

#include <cstdint>
#include <string>

namespace voxel::world
{

struct ItemStack
{
    std::string name;
    uint16_t count = 0;

    ItemStack() = default;
    ItemStack(std::string itemName, uint16_t itemCount);

    [[nodiscard]] const std::string& getName() const { return name; }
    [[nodiscard]] uint16_t getCount() const { return count; }
    void setCount(uint16_t c) { count = c; }
    [[nodiscard]] bool isEmpty() const { return name.empty() || count == 0; }
    void clear()
    {
        name.clear();
        count = 0;
    }
};

} // namespace voxel::world
