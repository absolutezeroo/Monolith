// Block.cpp — BlockCallbacksDeleter definition.
// Defined here because Block.h forward-declares BlockCallbacks and
// the custom deleter needs the complete type to call delete.

#include "voxel/world/Block.h"

#include "voxel/scripting/BlockCallbacks.h"

namespace voxel::world
{

void BlockCallbacksDeleter::operator()(voxel::scripting::BlockCallbacks* p) const
{
    delete p;
}

} // namespace voxel::world
