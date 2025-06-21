file(GLOB ENGINE_SRC CONFIGURE_DEPENDS
    *.cpp *.h
    Renderer/*.cpp Renderer/*.h
    ${CMAKE_SOURCE_DIR}/ext/vk-bootstrap/src/VkBootstrap.cpp
    
)

add_library(Engine ${ENGINE_SRC})

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ext/vk-bootstrap/src
    ${CMAKE_SOURCE_DIR}/ext/tinygltf
)

target_link_libraries(Engine PUBLIC
    glfw
    Vulkan::Vulkan
)
