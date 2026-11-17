add_executable(Xora2D-Sandbox
    src/main.cpp
)

target_link_libraries(Xora2D-Sandbox PRIVATE Xora2D::Xora2D)
target_compile_features(Xora2D-Sandbox PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(Xora2D-Sandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()
