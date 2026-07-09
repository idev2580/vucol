#include <socl/ShaderPipeline.hpp>

#include <utility>

namespace socl{
    namespace detail{
        ShaderPipelineState::~ShaderPipelineState(){
            if(device){
                if(pipeline){
                    device.destroyPipeline(pipeline);
                }
                if(pipelineLayout){
                    device.destroyPipelineLayout(pipelineLayout);
                }
                if(descriptorSetLayout){
                    device.destroyDescriptorSetLayout(descriptorSetLayout);
                }
                if(shaderModule){
                    device.destroyShaderModule(shaderModule);
                }
            }
        }
    }

    ShaderPipeline::ShaderPipeline() = default;
    ShaderPipeline::~ShaderPipeline() = default;

    ShaderPipeline::ShaderPipeline(std::shared_ptr<detail::ShaderPipelineState> state)
        : state_(std::move(state)){
    }

    std::span<const DescriptorBinding> ShaderPipeline::bindings() const{
        if(!state_){
            return {};
        }
        return state_->bindings;
    }

    std::uint32_t ShaderPipeline::pushConstantSize() const{
        return state_ ? state_->pushConstantSize : 0;
    }

    ShaderPipeline::operator bool() const{
        return static_cast<bool>(state_);
    }
}
