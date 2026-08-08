#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <socl/Buffer.hpp>
#include <socl/ShaderPipeline.hpp>
#include <vulkan/vulkan.hpp>

namespace socl{
    // Describes how one dispatch accesses a bound buffer. SOCL uses this
    // information to retain the buffer and insert dependencies between dispatches.
    enum class BufferAccess{
        Read,
        Write,
        ReadWrite,
    };

    namespace detail{
        struct DescriptorBufferBinding{
            std::shared_ptr<BufferState> buffer;
            BufferAccess access = BufferAccess::ReadWrite;
        };

        struct DescriptorSetState{
            std::shared_ptr<ContextState> context;
            std::shared_ptr<ShaderPipelineState> pipeline;
            vk::Device device;
            vk::DescriptorPool descriptorPool;
            vk::DescriptorSet descriptorSet;
            std::vector<DescriptorBufferBinding> buffers;

            ~DescriptorSetState();
        };
    }

    // DescriptorSet is the per-dispatch binding table for shader-visible resources.
    // A reusable ShaderPipeline defines the layout; each DescriptorSet supplies
    // the actual Buffer objects used for one run.
    class DescriptorSet{
        friend class Context;

        public:
        DescriptorSet();
        ~DescriptorSet();

        DescriptorSet(const DescriptorSet&) = default;
        DescriptorSet& operator=(const DescriptorSet&) = default;
        DescriptorSet(DescriptorSet&&) noexcept = default;
        DescriptorSet& operator=(DescriptorSet&&) noexcept = default;

        void bindBuffer(std::uint32_t binding,
                        const Buffer& buffer,
                        BufferAccess access = BufferAccess::ReadWrite);
        // Kept for explicit descriptor updates. Context::dispatch() captures
        // current logical bindings itself, so snapshot dispatch does not require it.
        void update();

        [[nodiscard]] explicit operator bool() const;

        private:
        explicit DescriptorSet(std::shared_ptr<detail::DescriptorSetState> state);
        std::shared_ptr<detail::DescriptorSetState> state_;
    };
}
