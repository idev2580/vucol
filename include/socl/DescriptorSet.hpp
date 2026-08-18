#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <socl/Buffer.hpp>
#include <socl/ShaderPipeline.hpp>
#include <vulkan/vulkan.hpp>

namespace socl{
    /**
     * @brief Declares how a dispatch accesses a bound buffer.
     *
     * SOCL uses this declaration to reject conflicting CPU/GPU accesses and insert
     * dependencies between dispatches. Supplying a weaker mode than the shader's
     * actual access is invalid and can defeat those safeguards.
     */
    enum class BufferAccess{
        Read,      ///< The shader reads but does not write the buffer.
        Write,     ///< The shader writes the buffer; reads are not declared.
        ReadWrite, ///< The shader may both read and write the buffer.
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

    /**
     * @brief Mutable logical binding table for shader-visible buffers.
     *
     * A reusable ShaderPipeline defines the layout and each DescriptorSet supplies
     * the buffers used for a run. Context::dispatch() copies the current logical
     * bindings into an immutable native descriptor set, allowing this object to be
     * rebound without changing an already recorded dispatch.
     *
     * Copies share the same logical binding table. The table and its pipeline remain
     * alive until the last handle or captured dispatch snapshot releases them.
     *
     * @par Thread safety
     * DescriptorSet and all aliases of it require external synchronization. Different
     * descriptor sets still must not concurrently use the same Context.
     */
    class DescriptorSet{
        friend class Context;

        public:
        /** @brief Constructs an empty descriptor-set handle. */
        DescriptorSet();

        /** @brief Releases this shared handle and its logical bindings. */
        ~DescriptorSet();

        /**
         * @brief Shares another handle's logical binding table.
         * @param other Input handle to share.
         */
        DescriptorSet(const DescriptorSet& other) = default;

        /**
         * @brief Replaces this handle with another shared binding table.
         * @param other Input handle to share.
         * @return This handle.
         */
        DescriptorSet& operator=(const DescriptorSet& other) = default;

        /**
         * @brief Transfers a descriptor-set handle.
         * @param other Input handle, empty after the move.
         */
        DescriptorSet(DescriptorSet&& other) noexcept = default;

        /**
         * @brief Replaces this handle by moving another handle into it.
         * @param other Input handle, empty after the move.
         * @return This handle.
         */
        DescriptorSet& operator=(DescriptorSet&& other) noexcept = default;

        /**
         * @brief Assigns a buffer to one shader binding.
         * @param binding Binding number declared in the associated pipeline.
         * @param buffer Buffer to bind. It must belong to the same Context.
         * @param access Shader access that the dispatch will perform on @p buffer.
         * @throws std::runtime_error If either handle is empty, the contexts differ,
         *         or @p binding is absent from the pipeline layout.
         * @par Synchronization
         * Synchronous host-side state update; no GPU command is recorded or submitted.
         * @par Thread safety
         * Not safe concurrently with any access to this descriptor set or an alias.
         * @par Resource safety
         * The table retains the buffer. Each dispatch takes its own immutable snapshot,
         * so rebinding does not mutate descriptor data already recorded for GPU use.
         */
        void bindBuffer(std::uint32_t binding,
                        const Buffer& buffer,
                        BufferAccess access = BufferAccess::ReadWrite);

        /**
         * @brief Writes the logical bindings into this object's native descriptor set.
         * @throws std::runtime_error If the handle is empty or a binding is unassigned.
         * @par Synchronization
         * Synchronous host-side Vulkan descriptor update. This call is not required by
         * Context::dispatch(), which creates and updates an immutable snapshot itself.
         * @par Thread safety
         * Not safe concurrently with binding changes or other uses of this object.
         * @par Resource safety
         * Dispatch snapshots own separate native descriptor sets, so updating this
         * logical set does not rewrite a snapshot already in use by the GPU.
         */
        void update();

        /**
         * @brief Tests whether this handle contains a descriptor set.
         * @return `true` for a non-empty handle; otherwise `false`.
         * @par Thread safety
         * Safe when no thread concurrently modifies this handle.
         */
        [[nodiscard]] explicit operator bool() const;

        private:
        explicit DescriptorSet(std::shared_ptr<detail::DescriptorSetState> state);
        std::shared_ptr<detail::DescriptorSetState> state_;
    };
}
