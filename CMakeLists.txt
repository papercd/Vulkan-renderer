cmake_minimum_required(VERSION 3.15) 
project(VulkanGameEngine) 

set(CMAKE_CXX_STANDARD 17) 
set(CMAKE_CXX_STANDARD_REQUIRED ON) 

set_property(GLOBAL PROPERTY USE_FOLDERS ON) 

find_package(Vulkan REQUIRED) 

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(ext/glfw) 
add_subdirectory(ext/bullet) 

include_directories(
	${Vulkan_INCLUDE_DIRS}
	ext/glm
	ext/stb
	ext/vk-bootstrap
	ext/tinyobjloader
	ext/tinygltf
)

add_subdirectory(engine)

add_executable(VulkanGameEngine main.cpp)
target_link_libraries(VulkanGameEngine
    Vulkan::Vulkan
    glfw
    BulletDynamics BulletCollision LinearMath
    Engine
)
