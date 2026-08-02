#include <socl/ShaderCompiler.hpp>

#include <shaderc/shaderc.hpp>

#include <stdexcept>
#include <string>

namespace socl{
    namespace{
        shaderc_env_version toShadercVulkanVersion(VulkanVersion version){
            switch(version){
                case VulkanVersion::Vulkan10:
                    return shaderc_env_version_vulkan_1_0;
                case VulkanVersion::Vulkan11:
                    return shaderc_env_version_vulkan_1_1;
                case VulkanVersion::Vulkan12:
                    return shaderc_env_version_vulkan_1_2;
                case VulkanVersion::Vulkan13:
                    return shaderc_env_version_vulkan_1_3;
            }
            throw std::runtime_error("Unsupported Vulkan shader target version.");
        }

        shaderc_spirv_version toShadercSpirvVersion(SpirvVersion version){
            switch(version){
                case SpirvVersion::Spirv10:
                    return shaderc_spirv_version_1_0;
                case SpirvVersion::Spirv11:
                    return shaderc_spirv_version_1_1;
                case SpirvVersion::Spirv12:
                    return shaderc_spirv_version_1_2;
                case SpirvVersion::Spirv13:
                    return shaderc_spirv_version_1_3;
                case SpirvVersion::Spirv14:
                    return shaderc_spirv_version_1_4;
                case SpirvVersion::Spirv15:
                    return shaderc_spirv_version_1_5;
                case SpirvVersion::Spirv16:
                    return shaderc_spirv_version_1_6;
            }
            throw std::runtime_error("Unsupported SPIR-V target version.");
        }

        std::vector<std::uint32_t> compileGlslToSpirvImpl(
            std::string_view source,
            std::string_view sourceName,
            const ShaderCompileOptions* options){
            if(source.empty()){
                throw std::runtime_error("GLSL shader source is empty.");
            }

            const std::string sourceNameString(sourceName);
            shaderc::Compiler compiler;
            if(!compiler.IsValid()){
                throw std::runtime_error("shaderc compiler initialization failed.");
            }

            shaderc::CompileOptions shadercOptions;
            if(options){
                shadercOptions.SetTargetEnvironment(
                    shaderc_target_env_vulkan,
                    toShadercVulkanVersion(options->vulkanVersion));
                shadercOptions.SetTargetSpirv(toShadercSpirvVersion(options->spirvVersion));
            }

            const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                source.data(),
                source.size(),
                shaderc_compute_shader,
                sourceNameString.c_str(),
                shadercOptions);

            if(result.GetCompilationStatus() != shaderc_compilation_status_success){
                const std::string error = result.GetErrorMessage();
                throw std::runtime_error(std::string("GLSL to SPIR-V compilation failed: ") +
                                         (error.empty() ? "unknown shaderc error" : error));
            }

            std::vector<std::uint32_t> spirv(result.cbegin(), result.cend());
            if(spirv.empty()){
                throw std::runtime_error("shaderc returned empty SPIR-V bytecode.");
            }
            return spirv;
        }
    }

    std::vector<std::uint32_t> compileGlslToSpirv(std::string_view source,
                                                  std::string_view sourceName){
        return compileGlslToSpirvImpl(source, sourceName, nullptr);
    }

    std::vector<std::uint32_t> compileGlslToSpirv(
        std::string_view source,
        const ShaderCompileOptions& options,
        std::string_view sourceName){
        return compileGlslToSpirvImpl(source, sourceName, &options);
    }
}
