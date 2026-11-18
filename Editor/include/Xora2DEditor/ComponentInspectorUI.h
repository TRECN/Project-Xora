#pragma once

#include "Xora2DEditor/EditorContext.h"
#include "Xora2DEditor/EditorSceneCommands.h"

#include "Xora2D/Scene/Entity.h"

namespace Xora2DEditor {

void DrawEntityInspector(Xora2D::Entity entity, EditorContext& context);
void DrawAddComponentMenu(Xora2D::Entity entity, EditorContext& context);

} // namespace Xora2DEditor