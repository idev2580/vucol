#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <iosfwd>
#include <optional>
#include <socl/Buffer.hpp>
#include <socl/DescriptorSet.hpp>
#include <socl/ShaderPipeline.hpp>
#include <string>
#include <string_view>
#include <vulkan/vulkan.hpp>
#include <vector>

namespace socl{
    /** @brief Floating-point GPU duration with nanosecond units. */
    using GpuDuration = std::chrono::duration<double, std::nano>;

    /** @brief Timestamp-query capabilities for the selected compute queue. */
    struct GpuTimingSupportInfo{
        bool synchronization2Supported = false; ///< Whether the GPU supports synchronization2.
        bool synchronization2Enabled = false; ///< Whether Context enabled synchronization2 automatically.
        bool timestampSupported = false; ///< Whether the selected compute queue supports timestamps.
        std::uint32_t timestampValidBits = 0; ///< Number of valid timestamp counter bits.
        float timestampPeriodNanoseconds = 0.0f; ///< Nanoseconds represented by one timestamp tick.
    };

    /** @brief Descriptive and queue information for one enumerated GPU. */
    struct GpuInfo{
        std::uint32_t index = 0; ///< Index accepted by ContextCreateInfo::physicalDeviceIndex.
        std::string name; ///< Vulkan physical-device name.
        std::string type; ///< Human-readable physical-device type.
        std::uint32_t vendorId = 0; ///< PCI-style vendor identifier reported by Vulkan.
        std::uint32_t deviceId = 0; ///< Vendor-defined device identifier.
        std::uint32_t apiVersion = 0; ///< Supported Vulkan API version in packed Vulkan form.
        std::uint32_t driverVersion = 0; ///< Vendor-defined packed driver version.
        std::uint32_t computeQueueFamily = 0; ///< Selected compute queue-family index.
        std::uint32_t computeQueueCount = 0; ///< Number of queues exposed by that family.
    };

    /** @brief Options used to create a Context. */
    struct ContextCreateInfo{
        std::uint32_t physicalDeviceIndex = 0; ///< Index from enumerateGpus().
        std::vector<const char*> requiredDeviceExtensions; ///< Required null-terminated Vulkan extension names.
    };

    /** @brief One cooperative-matrix tile shape and component-type combination. */
    struct CooperativeMatrixTileProperties{
        std::uint32_t m = 0; ///< Result row count.
        std::uint32_t n = 0; ///< Result column count.
        std::uint32_t k = 0; ///< Inner matrix dimension.
        vk::ComponentTypeKHR aType = vk::ComponentTypeKHR::eFloat16; ///< Matrix A component type.
        vk::ComponentTypeKHR bType = vk::ComponentTypeKHR::eFloat16; ///< Matrix B component type.
        vk::ComponentTypeKHR cType = vk::ComponentTypeKHR::eFloat16; ///< Accumulator component type.
        vk::ComponentTypeKHR resultType = vk::ComponentTypeKHR::eFloat16; ///< Result component type.
        bool saturatingAccumulation = false; ///< Whether saturating accumulation is used.
        vk::ScopeKHR scope = vk::ScopeKHR::eSubgroup; ///< Vulkan execution scope.
    };

    /** @brief Cooperative-matrix capabilities queried for the selected GPU. */
    struct CooperativeMatrixSupportInfo{
        bool extensionSupported = false; ///< Whether the device advertises the extension.
        bool featureSupported = false; ///< Whether the cooperative-matrix feature is supported.
        bool robustBufferAccessSupported = false; ///< Robust buffer access capability for cooperative matrices.
        vk::ShaderStageFlags supportedStages; ///< Shader stages supporting the feature.
        std::vector<CooperativeMatrixTileProperties> tiles; ///< Supported tile/type combinations.
    };

    /** @brief Subgroup capabilities queried and enabled for the selected GPU. */
    struct SubgroupSupportInfo{
        std::uint32_t defaultSize = 0; ///< Implementation's default subgroup size.
        vk::ShaderStageFlags supportedStages; ///< Stages supporting subgroup operations.
        vk::SubgroupFeatureFlags supportedOperations; ///< Supported subgroup operation categories.
        vk::ShaderStageFlags requiredSizeStages; ///< Stages supporting a required subgroup size.
        bool sizeControlSupported = false; ///< Whether subgroup-size control is supported.
        bool sizeControlEnabled = false; ///< Whether subgroup-size control was enabled on the device.
        bool computeFullSubgroupsSupported = false; ///< Whether full compute subgroups are supported.
        bool computeFullSubgroupsEnabled = false; ///< Whether full compute subgroups were enabled.
        std::uint32_t minSize = 0; ///< Minimum controllable subgroup size.
        std::uint32_t maxSize = 0; ///< Maximum controllable subgroup size.
    };

    namespace detail{
        struct DispatchResources;
        struct ContextState;
    }

    /**
     * @brief Move-only completion and lifetime token for one submitted command batch.
     *
     * The token owns the submission fence and command buffer and strongly retains all
     * captured descriptor snapshots, pipelines, buffers, and optional timestamp query.
     * wait(), destruction, or move-assignment of a valid token waits for completion
     * before releasing them.
     *
     * @par Thread safety
     * A token is not thread-safe. All access, including destruction, requires external
     * synchronization. It also shares its Context's command pool and device.
     */
    class DispatchToken{
        friend class Context;

        public:
        /** @brief Constructs an invalid token with no submission. */
        DispatchToken();

        /**
         * @brief Waits for a valid submission and then releases its retained resources.
         * @note Destruction of a valid token is synchronous and may block indefinitely.
         */
        ~DispatchToken();

        /**
         * @brief Copying is disabled because submission ownership is unique.
         * @param other Source token, which cannot be copied.
         */
        DispatchToken(const DispatchToken& other) = delete;
        /**
         * @brief Copy assignment is disabled because submission ownership is unique.
         * @param other Source token, which cannot be copied.
         * @return This token; the function is deleted and cannot be called.
         */
        DispatchToken& operator=(const DispatchToken& other) = delete;

        /**
         * @brief Transfers ownership of @p other submission without waiting.
         * @param other Source token, invalid after the move.
         */
        DispatchToken(DispatchToken&& other) noexcept;

        /**
         * @brief Replaces this token with @p other submission.
         * @param other Source token, invalid after the move.
         * @return This token.
         * @note Synchronously waits first if this token already owns a submission.
         */
        DispatchToken& operator=(DispatchToken&& other) noexcept;

        /**
         * @brief Waits until this submission completes and releases its resources.
         * Calling wait() on an invalid token is a no-op.
         * @throws std::runtime_error If the Vulkan fence wait or timed query retrieval fails.
         * @par Synchronization
         * Synchronous and blocking with an infinite fence timeout.
         * For a timed submission, the duration is cached before resources are released.
         * @par Thread safety
         * Not safe concurrently with any operation on this token or its Context.
         */
        void wait();

        /**
         * @brief Waits for a timed submission and returns its measured GPU duration.
         * @return Elapsed device time between the batch's top- and bottom-of-pipe timestamps.
         * @throws std::runtime_error If this token did not come from beginTimed(), or if
         *         fence waiting or timestamp result retrieval fails.
         * @par Synchronization
         * Synchronous and blocking with an infinite fence timeout. If wait() already
         * completed, this returns the duration cached during that wait.
         * @par Thread safety
         * Not safe concurrently with any operation on this token or its Context.
         */
        [[nodiscard]] GpuDuration waitAndGetGpuDuration();

        /**
         * @brief Tests whether this token owns a pending or uncollected submission.
         * @return `true` until wait() completes or the token is moved from.
         * @par Synchronization
         * Non-blocking; this does not query whether the GPU has completed.
         * @par Thread safety
         * Safe only when no thread concurrently modifies this token.
         */
        [[nodiscard]] bool valid() const;

        private:
        explicit DispatchToken(std::shared_ptr<detail::ContextState> context,
                               vk::Fence fence,
                               vk::CommandBuffer commandBuffer,
                               std::shared_ptr<detail::DispatchResources> resources);

        std::shared_ptr<detail::ContextState> context_;
        std::shared_ptr<detail::DispatchResources> resources_;
        vk::Fence fence_;
        vk::CommandBuffer commandBuffer_;
        std::optional<GpuDuration> gpuDuration_;
    };

    /**
     * @brief Owns one Vulkan device, compute queue, allocator, and recording state.
     *
     * Resources created from a Context share ownership of its internal device state,
     * so they can outlive the Context handle. Final device teardown waits for the GPU
     * to become idle. A Context records at most one command batch at a time.
     *
     * @par Thread safety
     * Context and all resources created from it are not internally synchronized.
     * Serialize operations that touch the same Context, queue, command pool, or shared
     * resource. Completely independent contexts may be used concurrently.
     *
     * @see @ref tutorial_axpy "Tutorial: AXPY and Batched Dispatch"
     * @see @ref dispatch_snapshots "Dispatch Snapshots and Batched Submission"
     */
    class Context{
        public:
        /**
         * @brief Creates a context for GPU index zero with no required extensions.
         * @throws std::runtime_error If Vulkan instance, device, queue, allocator, or
         *         command-pool initialization fails.
         * @par Synchronization
         * Synchronous; initialization is complete on return.
         */
        Context();

        /**
         * @brief Creates a context using explicit device-selection requirements.
         * @param createInfo Input GPU index and required device extensions. Extension
         *        name strings need only remain valid during construction.
         * @throws std::out_of_range If physicalDeviceIndex is unavailable.
         * @throws std::runtime_error If selection or Vulkan resource creation fails.
         * @par Synchronization
         * Synchronous; initialization is complete on return.
         */
        explicit Context(const ContextCreateInfo& createInfo);

        /**
         * @brief Releases the Context handle and any unfinished recording.
         * @note Device state remains alive while child resources or tokens retain it;
         *       final device destruction waits for the device to become idle.
         */
        ~Context();

        /**
         * @brief Copying is disabled because recording state has one owner.
         * @param other Source Context, which cannot be copied.
         */
        Context(const Context& other) = delete;
        /**
         * @brief Copy assignment is disabled because recording state has one owner.
         * @param other Source Context, which cannot be copied.
         * @return This Context; the function is deleted and cannot be called.
         */
        Context& operator=(const Context& other) = delete;
        /**
         * @brief Transfers a Context handle and its recording state.
         * @param other Input Context, empty after the move.
         */
        Context(Context&& other) noexcept = default;

        /**
         * @brief Replaces this Context by moving another Context into it.
         * @param other Input Context, empty after the move.
         * @return This Context.
         */
        Context& operator=(Context&& other) noexcept = default;

        /**
         * @brief Allocates a GPU buffer owned by this context.
         * @param bytes Requested allocation size in bytes; must be greater than zero.
         * @param type Preferred memory placement. Auto resolves from the GPU type.
         * @return A non-empty shared Buffer handle.
         * @throws std::runtime_error If @p bytes is zero or allocation fails.
         * @par Synchronization
         * Synchronous; the Vulkan buffer and allocation exist on return.
         * @par Thread safety
         * Not safe concurrently with other operations on this Context.
         */
        Buffer createBuffer(std::size_t bytes, BufferType type = BufferType::Auto);

        /**
         * @brief Creates a reusable compute pipeline from SPIR-V.
         * @param createInfo Input bytecode, layout, constants, entry point, and subgroup request.
         * @return A non-empty ShaderPipeline sharing this Context's lifetime.
         * @throws std::runtime_error If bytecode is empty, subgroup requirements are
         *         unsupported or invalid, or Vulkan pipeline creation fails.
         * @par Synchronization
         * Synchronous; all input data is consumed before return.
         * @par Thread safety
         * Not safe concurrently with other operations on this Context.
         */
        ShaderPipeline createShaderPipeline(const ShaderPipelineCreateInfo& createInfo);

        /**
         * @brief Creates a mutable binding table for a pipeline.
         * @param pipeline Input pipeline created by this Context; it must be non-empty.
         * @return An initially unbound DescriptorSet matching the pipeline layout.
         * @throws std::runtime_error If @p pipeline is empty or Vulkan allocation fails.
         * @par Synchronization
         * Synchronous; the native descriptor pool and set exist on return.
         * @par Thread safety
         * Not safe concurrently with other operations on this Context.
         */
        DescriptorSet createDescriptorSet(const ShaderPipeline& pipeline);

        /**
         * @brief Starts recording a one-time compute command batch.
         * @throws std::runtime_error If this Context is already recording.
         * @par Synchronization
         * Synchronous CPU-side recording setup; no GPU work is submitted.
         * @par Resource safety
         * Creates a batch tracker that retains resources claimed by later dispatches.
         * @see @ref dispatch_snapshots "Dispatch Snapshots and Batched Submission"
         */
        void begin();

        /**
         * @brief Starts a timestamped one-time compute command batch.
         * @throws std::runtime_error If this Context is already recording or GPU timing
         *         is unavailable on the selected device and compute queue.
         * @par Timing scope
         * Records a top-of-pipe timestamp before application commands. submitAsync()
         * records a bottom-of-pipe timestamp, so the result covers all commands and
         * automatic barriers recorded in this batch but excludes CPU submission and
         * queue-wait latency before the command buffer begins.
         * @par Feature activation
         * Context automatically queries and enables synchronization2 when supported;
         * callers do not need to request an extension or feature manually.
         */
        void beginTimed();

        /**
         * @brief Selects and records binding of a compute pipeline.
         * @param pipeline Input pipeline to use; it must be non-empty and belong to
         *        this Context.
         * @throws std::runtime_error If begin() was not called or the handle is empty.
         * @par Synchronization
         * Synchronous command recording only; no GPU work starts.
         * @par Thread safety
         * Not safe concurrently with any operation on this Context.
         */
        void use(const ShaderPipeline& pipeline);

        /**
         * @brief Selects the logical descriptor set for subsequent dispatches.
         * @param descriptorSet Input set created for the current pipeline and Context.
         *        If no pipeline is selected, its pipeline is selected automatically.
         * @throws std::runtime_error If not recording, the set is empty, belongs to a
         *         different Context, or is incompatible with the selected pipeline.
         * @par Synchronization
         * Synchronous state selection; dispatch() later captures the bindings.
         * @par Resource safety
         * Selection alone does not freeze bindings. dispatch() creates the immutable
         * native descriptor snapshot retained through GPU completion.
         */
        void bind(const DescriptorSet& descriptorSet);

        /**
         * @brief Records raw push-constant bytes for the selected pipeline.
         * @param data Input address containing @p bytes bytes. It may be null only when
         *        @p bytes is zero.
         * @param bytes Number of bytes to copy into the command buffer.
         * @param offset Destination byte offset in the declared push-constant range.
         * @throws std::runtime_error If not recording, no pipeline is selected, or the
         *         requested range exceeds ShaderPipelineCreateInfo::pushConstantSize.
         * @par Synchronization
         * Synchronously copies the values into recorded command data; no GPU work starts.
         */
        void push(const void* data, std::size_t bytes, std::size_t offset = 0);

        /**
         * @brief Records one trivially represented value at push-constant offset zero.
         * @tparam T Input value type; the Vulkan shader must use a matching layout.
         * @param value Input value copied as `sizeof(T)` raw bytes.
         * @throws std::runtime_error Under the same conditions as the raw push() overload.
         * @par Synchronization
         * Synchronous command recording only; no GPU work starts.
         */
        template<typename T>
        void push(const T& value){
            push(&value, sizeof(T), 0);
        }

        /**
         * @brief Snapshots the current buffers and records one complete compute dispatch.
         * @param groupCountX Number of workgroups in the X dimension.
         * @param groupCountY Number of workgroups in the Y dimension.
         * @param groupCountZ Number of workgroups in the Z dimension.
         * @throws std::runtime_error If required recording, pipeline, descriptor, or
         *         buffer state is missing, incompatible, or conflicts with another batch.
         * @par Recording model
         * Every call immediately copies the current logical DescriptorSet bindings into
         * a new immutable native descriptor snapshot. It then records both the command
         * that binds that snapshot and the compute dispatch command. Rebinding the same
         * logical DescriptorSet before a later dispatch therefore produces a separate
         * command pair and does not change any earlier dispatch:
         * @code
         * bind snapshot using buffer A
         * dispatch
         * bind snapshot using buffer B
         * dispatch
         * @endcode
         * submitAsync() does not reconstruct or change these per-dispatch bindings.
         * @par Synchronization
         * Synchronous command recording; execution begins only after submission.
         * Conflicting buffer accesses between dispatches in this batch receive Vulkan
         * compute-to-compute memory barriers; read-after-read needs no barrier.
         * @par Resource safety
         * Captures immutable descriptors, retains every pipeline and buffer, merges
         * duplicate buffer access modes, and claims buffers against conflicting CPU or
         * other recorded/in-flight GPU access until the batch resources are released.
         * @see @ref dispatch_snapshots "Dispatch Snapshots and Batched Submission"
         */
        void dispatch(std::uint32_t groupCountX,
                      std::uint32_t groupCountY = 1,
                      std::uint32_t groupCountZ = 1);

        /**
         * @brief Submits the already recorded command sequence once for asynchronous execution.
         * @return Move-only token for waiting and retaining the batch resources.
         * @throws std::runtime_error If no command buffer is recording or submission setup fails.
         * @par Submission model
         * This method does not iterate over dispatches, rebind application buffers, or
         * create new dispatch commands. All descriptor snapshot binds and dispatches were
         * already recorded by their respective dispatch() calls. This method ends that
         * command buffer and performs one queue submission. The GPU subsequently executes
         * the recorded sequence in order, switching to each dispatch's immutable buffer
         * snapshot without further CPU involvement.
         * @par Synchronization
         * Queue submission occurs before return, but GPU execution may continue afterward.
         * @par Resource safety
         * Ownership of snapshots, pipelines, buffers, an optional timestamp query, the
         * command buffer, and fence is transferred to the returned token. Discarding a
         * valid token waits in its destructor.
         * @see @ref dispatch_snapshots "Dispatch Snapshots and Batched Submission"
         */
        DispatchToken submitAsync();

        /**
         * @brief Submits the current batch and waits for GPU completion.
         * @throws std::runtime_error Under the same conditions as submitAsync() or if
         *         the completion fence wait fails.
         * @par Synchronization
         * Synchronous and blocking; all recorded work is complete on return.
         * @par Resource safety
         * Releases resource claims and captured snapshots only after fence completion.
         * @note For a timed batch, use submitAsync() followed by
         *       DispatchToken::waitAndGetGpuDuration() to retain the measured value.
         */
        void submitAndWait();

        /**
         * @brief Reports whether the selected physical device is an integrated GPU.
         * @return `true` for Vulkan's integrated-GPU device type.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] bool usingIntegratedGpu() const;

        /**
         * @brief Reports whether timed command batches can be recorded.
         * @return `true` when synchronization2 is enabled and the selected compute queue
         *         exposes a usable timestamp counter.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] bool supportsGpuTiming() const;

        /**
         * @brief Returns GPU timestamp and synchronization2 capability details.
         * @return Reference valid while the underlying Context state remains alive.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] const GpuTimingSupportInfo& gpuTimingSupportInfo() const;

        /**
         * @brief Returns information about the selected GPU.
         * @return Reference valid while the underlying Context state remains alive.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] const GpuInfo& gpuInfo() const;

        /**
         * @brief Copies the selected GPU's advertised device-extension names.
         * @return Owned vector of extension-name strings.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] std::vector<std::string> supportedDeviceExtensions() const;

        /**
         * @brief Tests whether the selected GPU advertises an extension.
         * @param extensionName Input extension name to search for.
         * @return `true` on an exact advertised-name match; otherwise `false`.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] bool supportsDeviceExtension(std::string_view extensionName) const;

        /**
         * @brief Reports usable cooperative-matrix support.
         * @return `true` when extension, feature, and at least one tile are available.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] bool supportsCooperativeMatrix() const;

        /**
         * @brief Returns cooperative-matrix capabilities for the selected GPU.
         * @return Reference valid while the underlying Context state remains alive.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] const CooperativeMatrixSupportInfo& cooperativeMatrixSupportInfo() const;

        /**
         * @brief Returns subgroup capabilities for the selected GPU.
         * @return Reference valid while the underlying Context state remains alive.
         * @par Thread safety
         * Safe for concurrent reads when this Context is not being moved or destroyed.
         */
        [[nodiscard]] const SubgroupSupportInfo& subgroupSupportInfo() const;

        /**
         * @brief Writes a human-readable GPU information summary.
         * @param os Output stream receiving the formatted text.
         * @par Synchronization
         * Synchronous host output; the method does not access the GPU queue.
         * @par Thread safety
         * The Context is read-only, but callers must synchronize shared access to @p os.
         */
        void printGpuInfo(std::ostream& os) const;

        /**
         * @brief Enumerates compute-capable GPUs available to a temporary Vulkan instance.
         * @return GPU descriptions whose indices can select a Context device.
         * @throws std::runtime_error If instance creation, selection, or enumeration fails.
         * @par Synchronization
         * Synchronous; the temporary instance is destroyed before return.
         * @par Thread safety
         * Calls use independent local Vulkan instances and do not access Context state.
         */
        [[nodiscard]] static std::vector<GpuInfo> enumerateGpus();

        private:
        std::shared_ptr<detail::ContextState> state_;
        std::shared_ptr<detail::DispatchResources> recordingResources_;
    };
}
