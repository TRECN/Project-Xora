# Xora2D Python runtime (CPython 3.12+).
# Prefer a discoverable development install (Homebrew python@3.12 on macOS).
#
# Override for CI or packaging:
#   -DXORA2D_PYTHON_EXECUTABLE=/path/to/python3.12
#   -DXORA2D_PYTHON_INCLUDE_DIR=...
#   -DXORA2D_PYTHON_LIBRARY=...

set(XORA2D_PYTHON_VERSION "3.12" CACHE STRING "Minimum required Python version")

if(DEFINED XORA2D_PYTHON_EXECUTABLE)
    set(Python3_EXECUTABLE "${XORA2D_PYTHON_EXECUTABLE}")
endif()

if(APPLE AND NOT DEFINED XORA2D_PYTHON_EXECUTABLE)
    foreach(_brew_python "/opt/homebrew/opt/python@${XORA2D_PYTHON_VERSION}" "/usr/local/opt/python@${XORA2D_PYTHON_VERSION}")
        if(EXISTS "${_brew_python}/bin/python${XORA2D_PYTHON_VERSION}")
            set(Python3_EXECUTABLE "${_brew_python}/bin/python${XORA2D_PYTHON_VERSION}" CACHE FILEPATH "Python 3 interpreter for Xora2D")
            set(Python3_ROOT_DIR "${_brew_python}" CACHE PATH "Python 3 root for Xora2D")
            break()
        endif()
    endforeach()
endif()

set(Python3_FIND_STRATEGY LOCATION)
set(Python3_FIND_FRAMEWORK NEVER)

find_package(Python3 ${XORA2D_PYTHON_VERSION} COMPONENTS Interpreter Development REQUIRED)

if(NOT Python3_Development_FOUND)
    message(FATAL_ERROR
        "CPython ${XORA2D_PYTHON_VERSION}+ development headers are required for XORA2D_ENABLE_SCRIPTING=ON. "
        "On macOS: brew install python@${XORA2D_PYTHON_VERSION}")
endif()

message(STATUS "Xora2D scripting: Python ${Python3_VERSION} at ${Python3_EXECUTABLE}")