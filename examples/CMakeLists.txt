add_executable(hello hello/main.cpp)

target_link_libraries(hello PRIVATE Vortex::vortex)
