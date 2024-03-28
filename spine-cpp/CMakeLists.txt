cmake_minimum_required(VERSION 3.26)
project(ZHANGYANGRUI)

message("${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
	message("MSCV detected")
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
	set (CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS}")
else()
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -std=c89")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wnon-virtual-dtor -pedantic -std=c++17 -fno-exceptions -fno-rtti")
endif()

include_directories(include)

file(GLOB_RECURSE  HEAD  CONFIGURE_DEPENDS "*.h")
file(GLOB_RECURSE  SOURCE CONFIGURE_DEPENDS "*.cpp")
set(AllFile ${HEAD} ${SOURCE})


message("${CMAKE_CURRENT_SOURCE_DIR}")

foreach(fileItem ${AllFile})       
    # Get the DIRECTORY of the ${fileItem} and assign it to ${PARENT_DIR}
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)

    # Remove common directory prefix to make the var(GROUP)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")

    # Make sure we are using windows slashes
    string(REPLACE "/" "\\" GROUP "${GROUP}")

    source_group("${GROUP}" FILES "${fileItem}")
endforeach()

add_library(spine-cpp STATIC ${AllFile})
target_include_directories(spine-cpp PUBLIC spine-cpp/include)

# Install target
install(TARGETS spine-cpp EXPORT spine-cpp_TARGETS DESTINATION dist/lib)
install(FILES ${INCLUDES} DESTINATION dist/include)

# Export target
export(
	EXPORT spine-cpp_TARGETS
	FILE ${CMAKE_CURRENT_BINARY_DIR}/spine-cppTargets.cmake
	NAMESPACE "Spine::")