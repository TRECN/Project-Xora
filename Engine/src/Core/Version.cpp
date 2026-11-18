#include "Xora2D/Core/Version.h"

#ifndef XORA2D_VERSION_STRING
#define XORA2D_VERSION_STRING "0.0.0"
#endif

namespace Xora2D {

const char* GetEngineVersionString() {
    return XORA2D_VERSION_STRING;
}

} // namespace Xora2D
