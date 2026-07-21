set(SOURCES 
	"${CMAKE_CURRENT_SOURCE_DIR}/Buffer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Context.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/DescriptorSet.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompiler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ShaderPipeline.cpp"
)

# Export to parent
set(SOURCES ${SOURCES} PARENT_SCOPE)
