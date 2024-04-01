cmake_minimum_required(VERSION 3.20)
project(mask-analyzer LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
if(MSVC)
	message("MSCV detected")
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
	set (CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS}")
else()
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -std=c89")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wnon-virtual-dtor -pedantic -std=c++17 -fno-exceptions -fno-rtti")
endif()

add_subdirectory(glfw glfw)

find_package(OpenGL REQUIRED)
set(LIBS glfw3 ${OPENGL_LIBRARIES})

if(POLICY CMP0146)
  cmake_policy(SET CMP0146 OLD) 
endif()
find_package(CUDA REQUIRED)
set(LIBS ${LIBS} ${CUDA_LIBRARIES})
set(CMAKE_CUDA_STANDARD 17)

add_library(STB_IMAGE "stb_image/stb_image.cpp")
set(LIBS ${LIBS} STB_IMAGE)

add_library(GLAD glad/src/glad.c)
set(LIBS ${LIBS} GLAD)

add_subdirectory(spine-cpp spine-cpp)

include_directories(
    ${OPENGL_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIRS}
    spine-cpp/spine-cpp/include
	stb_image
    glad/include
    glfw/include
    include
)
file(GLOB_RECURSE HEAD CONFIGURE_DEPENDS "include/*.h" "include/*.cuh")
file(GLOB_RECURSE  SOURCE CONFIGURE_DEPENDS "src/*.cpp" "src/*.cu")
list(APPEND SOURCE "stb_image/stb_image.cpp")
set(AllFile ${HEAD} ${SOURCE})
message("HEAD:")
message("${HEAD}")

foreach(fileItem ${AllFile})       
    # Get the DIRECTORY of the ${fileItem} and assign it to ${PARENT_DIR}
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)

    # Remove common directory prefix to make the var(GROUP)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")

    # Make sure we are using windows slashes
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    
    message("${fileItem}")
    message("${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()


add_executable(mask-analyzer ${AllFile})

target_link_libraries(mask-analyzer
        ${LIBS}
        spine-cpp
        )