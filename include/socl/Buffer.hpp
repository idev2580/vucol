#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vulkan/vulkan.hpp>

namespace socl{
    /**
     * @brief Selects the preferred memory placement of a Buffer.
     *
     * This setting controls allocation only. DescriptorType controls how a shader
     * interprets the buffer.
     */
    enum class BufferType{
        Auto,        ///< Prefer host-visible memory on an iGPU and device-local memory otherwise.
        DeviceLocal, ///< Prefer device-local memory; CPU transfers use an internal staging buffer.
        HostVisible, ///< Prefer memory that can be mapped directly by the host.
    };

    namespace detail{
        struct ContextState;
        struct BufferState;
    }

    /**
     * @brief Shared handle to a GPU buffer owned by a Context.
     *
     * Copies share the same Vulkan buffer and allocation. The underlying resource
     * remains alive while any Buffer, descriptor snapshot, recorded batch, or
     * DispatchToken references it. CPU accesses are range checked and are rejected
     * when they conflict with a recorded or in-flight GPU access.
     *
     * @par Thread safety
     * Different buffers may be used concurrently when their owning contexts are not
     * being accessed concurrently. Concurrent access to the same Buffer state, or to
     * aliases of it, is not supported; external synchronization is required.
     */
    class Buffer{
        friend class Context;
        friend class DescriptorSet;

        public:
        /** @brief Constructs an empty buffer handle. */
        Buffer();

        /**
         * @brief Releases this handle.
         *
         * The GPU allocation is destroyed only after the last shared reference,
         * including references held by recorded or submitted dispatches, is released.
         */
        ~Buffer();

        /**
         * @brief Shares ownership of another buffer's state.
         * @param other Input handle to share.
         */
        Buffer(const Buffer& other) = default;

        /**
         * @brief Replaces this handle with shared ownership of another buffer.
         * @param other Input handle to share.
         * @return This handle.
         */
        Buffer& operator=(const Buffer& other) = default;

        /**
         * @brief Transfers a buffer handle without transferring GPU work.
         * @param other Input handle, empty after the move.
         */
        Buffer(Buffer&& other) noexcept = default;

        /**
         * @brief Replaces this handle by moving another handle into it.
         * @param other Input handle, empty after the move.
         * @return This handle.
         */
        Buffer& operator=(Buffer&& other) noexcept = default;

        /**
         * @brief Returns the allocation size.
         * @return Size in bytes, or `0` for an empty handle.
         * @par Thread safety
         * Safe for concurrent read-only calls if no thread moves from, assigns, or
         * destroys this handle at the same time.
         */
        [[nodiscard]] std::size_t size() const;

        /**
         * @brief Returns the resolved memory-placement type.
         * @return The resolved BufferType, or BufferType::Auto for an empty handle.
         * @par Thread safety
         * Safe under the same read-only conditions as size().
         */
        [[nodiscard]] BufferType type() const;

        /**
         * @brief Reports whether the allocation can be mapped by the CPU.
         * @return `true` when direct host mapping is available; otherwise `false`.
         * @par Thread safety
         * Safe under the same read-only conditions as size().
         */
        [[nodiscard]] bool hostVisible() const;

        /**
         * @brief Tests whether this handle contains a buffer.
         * @return `true` for a non-empty handle; otherwise `false`.
         * @par Thread safety
         * Safe when no thread concurrently modifies this handle.
         */
        [[nodiscard]] explicit operator bool() const;

        /**
         * @brief Copies CPU data into a range of the buffer.
         * @param data Input address containing at least @p bytes bytes. It may be null
         *        only when @p bytes is zero.
         * @param bytes Number of bytes to copy.
         * @param offset Destination byte offset in this buffer.
         * @throws std::runtime_error If the handle is empty, mapping or a Vulkan
         *         operation fails, or any recorded/in-flight GPU access claims the buffer.
         * @throws std::out_of_range If the requested range exceeds size().
         * @warning @p offset plus @p bytes must be representable by std::size_t.
         * @par Synchronization
         * Synchronous. Host-visible memory is updated before return. Device-local
         * memory is updated through a staging copy and the method waits for that copy.
         * @par Thread safety
         * Not safe to call concurrently on the same buffer or its aliases.
         * @par Resource safety
         * The operation refuses to race with GPU reads or writes. Staging resources
         * and their command buffer remain alive until the transfer fence completes.
         */
        void write(const void* data, std::size_t bytes, std::size_t offset = 0);

        /**
         * @brief Copies a range of the buffer into CPU memory.
         * @param data Output address with space for at least @p bytes bytes. It may be
         *        null only when @p bytes is zero.
         * @param bytes Number of bytes to copy.
         * @param offset Source byte offset in this buffer.
         * @throws std::runtime_error If the handle is empty, mapping or a Vulkan
         *         operation fails, or a recorded/in-flight GPU write claims the buffer.
         * @throws std::out_of_range If the requested range exceeds size().
         * @warning @p offset plus @p bytes must be representable by std::size_t.
         * @par Synchronization
         * Synchronous. Device-local memory is read through a staging copy whose fence
         * is waited before this method returns.
         * @par Thread safety
         * Not safe to call concurrently on the same buffer or its aliases.
         * @par Resource safety
         * Concurrent GPU readers are permitted, but a possible GPU writer causes the
         * operation to fail instead of exposing incomplete data.
         */
        void read(void* data, std::size_t bytes, std::size_t offset = 0) const;

        private:
        explicit Buffer(std::shared_ptr<detail::BufferState> state);
        std::shared_ptr<detail::BufferState> state_;
    };
}
