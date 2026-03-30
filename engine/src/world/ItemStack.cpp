#include "voxel/world/ItemStack.h"

namespace voxel::world
{

ItemStack::ItemStack(std::string itemName, uint16_t itemCount) : name(std::move(itemName)), count(itemCount) {}

} // namespace voxel::world
