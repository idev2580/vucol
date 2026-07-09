#pragma once

namespace socl{
    enum BufferType{
        Auto,       //dGPU -> DeviceLocal, iGPU -> HostVisible
        DeviceLocal,
        HostVisible,
    };
    class Buffer{

    };
}