#include "voxel/renderer/ModelRegistry.h"

#include "voxel/core/Log.h"
#include "voxel/renderer/ModelMesher.h"

namespace voxel::renderer
{

void ModelRegistry::getModelVertices(
    int x,
    int y,
    int z,
    const world::BlockDefinition& blockDef,
    const world::StateMap& state,
    uint8_t faceMask,
    std::vector<ModelVertex>& outVertices) const
{
    switch (blockDef.modelType)
    {
    case world::ModelType::Slab:
        ModelMesher::generateSlab(x, y, z, blockDef, state, 3, faceMask, outVertices);
        break;

    case world::ModelType::Cross:
        ModelMesher::generateCross(x, y, z, blockDef, outVertices);
        break;

    case world::ModelType::Torch:
        ModelMesher::generateTorch(x, y, z, blockDef, state, faceMask, outVertices);
        break;

    case world::ModelType::FullCube:
        // FullCube blocks use the quad path, not the model path.
        break;

    case world::ModelType::Stair:
    case world::ModelType::Connected:
    case world::ModelType::JsonModel:
    case world::ModelType::MeshModel:
    case world::ModelType::Custom:
        VX_LOG_WARN(
            "ModelRegistry: model type {} not yet implemented for block '{}'",
            static_cast<int>(blockDef.modelType),
            blockDef.stringId);
        break;
    }
}

} // namespace voxel::renderer
