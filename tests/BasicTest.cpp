#include <socl/Context.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderPipeline.hpp>

using namespace socl;
int main(){
    Context ctx;

    Buffer A = ctx.createBuffer(a_bytes);
    Buffer B = ctx.createBuffer(b_bytes);
    Buffer C = ctx.createBuffer(c_bytes);

    ShaderPipeline gemm = ctx.createShaderPipeline({
        .spirv = gemm_spv,
        .bindings = {
            {0, DescriptorType::UnifiedPreferred},
            {1, DescriptorType::UnifiedPreferred},
            {2, DescriptorType::UnifiedPreferred},
        },
        .pushConstantSize = sizeof(GemmArgs),
        .specConstants = {
            {0, tile_m},
            {1, tile_n},
            {2, tile_k},
        }
    });

    DescriptorSet desc = ctx.createDescriptorSet(gemm);
    desc.bindBuffer(0, A);
    desc.bindBuffer(1, B);
    desc.bindBuffer(2, C);
    desc.update();

    ctx.begin();
    ctx.use(gemm);
    ctx.bind(desc);
    ctx.push(args);
    ctx.dispatch(gx, gy, gz);
    ctx.submitAndWait();
}