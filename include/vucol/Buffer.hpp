#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vulkan/vulkan.hpp>

namespace vucol{
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

    /**
     * @brief Selects which other devices in the group may access a Buffer.
     *
     * Every Buffer is allocated on exactly one owner device
     * (see Buffer::ownerDeviceIndex()). The owner device always has full access.
     * These flags additionally allow other devices in the same device group to
     * read from, or write to, the buffer through peer memory. Requesting peer
     * access that the hardware does not support fails at buffer creation time.
     */
    enum class BufferPeerAccess : std::uint32_t{
        None = 0,         ///< No access from non-owner devices.
        PeerRead = 1u << 0,  ///< Non-owner devices may read (ALLOW_PEER_READ).
        PeerWrite = 1u << 1, ///< Non-owner devices may write (ALLOW_PEER_WRITE).
    };

    /** @brief Allows peer reads and peer writes (ALLOW_PEER_READ_WRITE). */
    constexpr BufferPeerAccess ALLOW_PEER_READ_WRITE =
        static_cast<BufferPeerAccess>(static_cast<std::uint32_t>(BufferPeerAccess::PeerRead) |
                                      static_cast<std::uint32_t>(BufferPeerAccess::PeerWrite));
    /** @brief Allows non-owner devices to read the buffer. */
    constexpr BufferPeerAccess ALLOW_PEER_READ = BufferPeerAccess::PeerRead;
    /** @brief Allows non-owner devices to write the buffer. */
    constexpr BufferPeerAccess ALLOW_PEER_WRITE = BufferPeerAccess::PeerWrite;

    /**
     * @brief Combines peer-access flags.
     * @param left First flag set.
     * @param right Second flag set.
     * @return Bitwise union of both sets.
     */
    [[nodiscard]] constexpr BufferPeerAccess operator|(BufferPeerAccess left,
                                                            BufferPeerAccess right){
        return static_cast<BufferPeerAccess>(static_cast<std::uint32_t>(left) |
                                             static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Intersects peer-access flags.
     * @param left First flag set.
     * @param right Second flag set.
     * @return Bitwise intersection of both sets.
     */
    [[nodiscard]] constexpr BufferPeerAccess operator&(BufferPeerAccess left,
                                                            BufferPeerAccess right){
        return static_cast<BufferPeerAccess>(static_cast<std::uint32_t>(left) &
                                             static_cast<std::uint32_t>(right));
    }

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
     * while any uncollected GPU submission references the buffer.
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
         * @brief Returns the device the buffer memory was allocated on.
         * @return Owner device index within the owning Context's group,
         *         or `0` for an empty handle.
         * @par Thread safety
         * Safe under the same read-only conditions as size().
         */
        [[nodiscard]] std::uint32_t ownerDeviceIndex() const;

        /**
         * @brief Returns the peer-access flags granted at creation.
         * @return The BufferPeerAccess value, or BufferPeerAccess::None for an
         *         empty handle.
         * @par Thread safety
         * Safe under the same read-only conditions as size().
         */
        [[nodiscard]] BufferPeerAccess peerAccess() const;

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
         *         operation fails, or an uncollected GPU submission claims the buffer.
         * @throws std::out_of_range If the requested range exceeds size().
         * @par Synchronization
         * Synchronous. Host-visible memory is updated before return. Device-local
         * memory is updated through a staging copy and the method waits for that copy.
         * @par Thread safety
         * Not safe to call concurrently on the same buffer or its aliases.
         * @par Resource safety
         * The operation refuses to overlap any submitted GPU use of this buffer.
         * Staging resources and their command buffer remain alive until the transfer
         * fence completes.
         */
        void write(const void* data, std::size_t bytes, std::size_t offset = 0);

        /**
         * @brief Copies a range of the buffer into CPU memory.
         * @param data Output address with space for at least @p bytes bytes. It may be
         *        null only when @p bytes is zero.
         * @param bytes Number of bytes to copy.
         * @param offset Source byte offset in this buffer.
         * @throws std::runtime_error If the handle is empty, mapping or a Vulkan
         *         operation fails, or an uncollected GPU submission claims the buffer.
         * @throws std::out_of_range If the requested range exceeds size().
         * @par Synchronization
         * Synchronous. Device-local memory is read through a staging copy whose fence
         * is waited before this method returns.
         * @par Thread safety
         * Not safe to call concurrently on the same buffer or its aliases.
         * @par Resource safety
         * The operation refuses to overlap GPU reads as well as GPU writes. Call
         * DispatchToken::wait() for every submission using this buffer before reading.
         */
        void read(void* data, std::size_t bytes, std::size_t offset = 0) const;

        private:
        explicit Buffer(std::shared_ptr<detail::BufferState> state);
        std::shared_ptr<detail::BufferState> state_;
    };
}
