#pragma once

namespace Xora2D {

class AssetManager;
class AudioEngine;
class InputManager;
class Scene;

// Per-frame services exposed to Python bindings (main thread only).
struct ScriptServices {
    Scene* Scene = nullptr;
    InputManager* Input = nullptr;
    AudioEngine* Audio = nullptr;
    AssetManager* Assets = nullptr;
};

} // namespace Xora2D