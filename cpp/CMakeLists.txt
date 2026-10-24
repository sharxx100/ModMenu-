cmake_minimum_required(VERSION 3.18.1)
project("modmenu")

# 1. ImGui Library
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/backends/imgui_impl_android.cpp
)
target_include_directories(imgui PUBLIC imgui imgui/backends)

# 2. Dobby Hook Library
add_library(dobby STATIC IMPORTED)
set_target_properties(dobby PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/dobby/libdobby.a)
target_include_directories(dobby INTERFACE ${CMAKE_SOURCE_DIR}/dobby)

# 3. Main Payload (.so)
add_library(menu SHARED payload.cpp)
target_link_libraries(menu 
    imgui 
    dobby 
    android 
    log 
    EGL 
    GLESv3 
    dl
)