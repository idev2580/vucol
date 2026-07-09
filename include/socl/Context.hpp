#pragma once
#include <cstddef>
#include <socl/Buffer.hpp>

namespace socl{
    class Context{
        
        public:
        Buffer createBuffer(std::size_t bytes);
        
    };
}