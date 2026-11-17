add_executable(Xora2D-Tests
    src/CoreTests.cpp
    src/GraphicsTests.cpp
    src/RendererTests.cpp
    src/SceneTests.cpp
    src/PhysicsTests.cpp
    src/SerializationTests.cpp
)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(Xora2D-Tests PRIVATE Xora2D::Xora2D SDL3::SDL3)
target_include_directories(Xora2D-Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/src
    ${CMAKE_SOURCE_DIR}/third_party/glad/include
)
target_compile_features(Xora2D-Tests PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(Xora2D-Tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME Xora2D-Tests COMMAND Xora2D-Tests)
