# Scan CPP files
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")

# Export to parent
set(TEST_SOURCES ${TEST_SOURCES} PARENT_SCOPE)