#include <socl/Buffer.hpp>

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace socl{
    namespace detail{
        BufferState::~BufferState(){
            if(allocator != VK_NULL_HANDLE && buffer){
                vmaDestroyBuffer(allocator, buffer, allocation);
            }
        }
    }

    Buffer::Buffer() = default;
    Buffer::~Buffer() = default;

    Buffer::Buffer(std::shared_ptr<detail::BufferState> state)
        : state_(std::move(state)){
    }

    std::size_t Buffer::size() const{
        return state_ ? static_cast<std::size_t>(state_->size) : 0;
    }

    BufferType Buffer::type() const{
        return state_ ? state_->type : BufferType::Auto;
    }

    bool Buffer::hostVisible() const{
        return state_ && state_->hostVisible;
    }

    Buffer::operator bool() const{
        return static_cast<bool>(state_);
    }

    void Buffer::write(const void* data, std::size_t bytes, std::size_t offset){
        if(!state_){
            throw std::runtime_error("Cannot write to an empty socl::Buffer.");
        }
        if(!state_->hostVisible){
            throw std::runtime_error("Buffer is not host visible.");
        }
        if(offset + bytes > size()){
            throw std::out_of_range("Buffer write range is out of bounds.");
        }

        void* mapped = nullptr;
        VkResult result = vmaMapMemory(state_->allocator, state_->allocation, &mapped);
        if(result != VK_SUCCESS){
            throw std::runtime_error("vmaMapMemory failed while writing buffer.");
        }

        std::memcpy(static_cast<std::byte*>(mapped) + offset, data, bytes);
        vmaUnmapMemory(state_->allocator, state_->allocation);
    }

    void Buffer::read(void* data, std::size_t bytes, std::size_t offset) const{
        if(!state_){
            throw std::runtime_error("Cannot read from an empty socl::Buffer.");
        }
        if(!state_->hostVisible){
            throw std::runtime_error("Buffer is not host visible.");
        }
        if(offset + bytes > size()){
            throw std::out_of_range("Buffer read range is out of bounds.");
        }

        void* mapped = nullptr;
        VkResult result = vmaMapMemory(state_->allocator, state_->allocation, &mapped);
        if(result != VK_SUCCESS){
            throw std::runtime_error("vmaMapMemory failed while reading buffer.");
        }

        std::memcpy(data, static_cast<std::byte*>(mapped) + offset, bytes);
        vmaUnmapMemory(state_->allocator, state_->allocation);
    }
}
