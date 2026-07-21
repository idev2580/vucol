#include <socl/ShaderCompiler.hpp>

#include <shaderc/shaderc.hpp>

#include <stdexcept>
#include <string>

namespace socl{
    std::vector<std::uint32_t> compileGlslToSpirv(std::string_view source,
                                                  std::string_view sourceName){
        if(source.empty()){
            throw std::runtime_error("GLSL shader source is empty.");
        }

        const std::string sourceNameString(sourceName);
        shaderc::Compiler compiler;
        if(!compiler.IsValid()){
            throw std::runtime_error("shaderc compiler initialization failed.");
        }

        const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            source.data(),
            source.size(),
            shaderc_compute_shader,
            sourceNameString.c_str());

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
