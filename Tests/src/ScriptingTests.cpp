#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"
#include "Xora2D/Scripting/PythonScriptEngine.h"
#include "Xora2D/Scripting/ScriptServices.h"
#include "Xora2D/Serialization/SceneSerializer.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_scriptingFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_scriptingFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

std::filesystem::path TempStatePath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

bool WriteAssetScript(const std::string& relativePath, const std::string& source) {
    const std::filesystem::path absolute = Xora2D::ResolveAssetPath(relativePath);
    std::error_code error;
    std::filesystem::create_directories(absolute.parent_path(), error);
    std::ofstream file(absolute);
    if (!file) {
        return false;
    }
    file << source;
    return static_cast<bool>(file);
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
}

const char* kStateHelper = R"(
    def _state_path(self):
        return getattr(self, "state_path", "")
    def _read_state(self):
        path = self._state_path()
        if not path:
            return ""
        try:
            with open(path, "r") as handle:
                return handle.read()
        except OSError:
            return ""
    def _write_state(self, value):
        path = self._state_path()
        if not path:
            return
        with open(path, "w") as handle:
            handle.write(value)
    def _append_state(self, token):
        path = self._state_path()
        if not path:
            return
        with open(path, "a") as handle:
            handle.write(token)
)";

struct ScriptingHarness {
    Xora2D::PythonScriptEngine engine;
    Xora2D::AssetRegistry registry;
    Xora2D::AssetManager assets{registry};
    std::shared_ptr<Xora2D::Scene> scene;

    bool Initialize() {
        if (!engine.IsAvailable()) {
            const Xora2D::ScriptLoadResult init = engine.Initialize();
            if (!init.Success) {
                return false;
            }
        }
        scene = Xora2D::Scene::Create();
        scene->SetAssetManager(&assets);
        scene->SetPythonScriptEngine(&engine);
        Xora2D::ScriptServices services{};
        services.Scene = scene.get();
        services.Assets = &assets;
        engine.SetServices(services);
        return true;
    }

    Xora2D::Entity AddScriptEntity(Xora2D::AssetHandle script, const std::string& className,
                                   const std::vector<Xora2D::ScriptProperty>& properties = {},
                                   bool enabled = true) {
        Xora2D::Entity entity = scene->CreateEntity(className + "Entity");
        auto& component = entity.AddComponent<Xora2D::PythonScriptComponent>();
        component.ScriptAsset = script;
        component.ClassName = className;
        component.Enabled = enabled;
        component.Properties = properties;
        return entity;
    }

    void Tick(int frames, float dt = 0.016f) {
        for (int frame = 0; frame < frames; ++frame) {
            scene->OnUpdate(dt);
        }
    }
};

Xora2D::AssetHandle RegisterAuditScript(Xora2D::AssetManager& assets, const char* relativePath,
                                        const char* displayName) {
    const Xora2D::AssetResult registration =
        assets.RegisterPythonScriptAtRelativePath(relativePath, displayName);
    CHECK(registration.Success, std::string("register ") + relativePath);
    return registration.Handle;
}

#if XORA2D_SCRIPTING_ENABLED

void TestLifecycleAndDestroy(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_lifecycle.py";
    WriteAssetScript(relative, std::string(R"(
class AuditLifecycle:
)") + kStateHelper + R"(
    def on_create(self):
        self._append_state("c")
    def on_update(self, dt):
        self._append_state("u")
    def on_destroy(self):
        self._append_state("d")
)");
    const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditLifecycle");
    const std::filesystem::path statePath = TempStatePath("xora2d_audit_lifecycle.txt");
    WriteTextFile(statePath, "");

    Xora2D::Entity entity = harness.AddScriptEntity(
        handle, "AuditLifecycle",
        {Xora2D::ScriptProperty{.Name = "state_path",
                                .Type = Xora2D::ScriptPropertyType::String,
                                .StringValue = statePath.string()}});

    harness.Tick(3);
    const std::string mid = ReadTextFile(statePath);
    CHECK(mid == "cuuu", "on_create once and on_update each frame");

    harness.scene->DestroyEntity(entity);
    harness.Tick(1);
  CHECK(ReadTextFile(statePath) == "cuuud", "on_destroy exactly once on entity removal");
}

void TestIndependentInstances(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_twin.py";
    WriteAssetScript(relative, std::string(R"(
class AuditTwin:
)") + kStateHelper + R"(
    def on_create(self):
        marker = getattr(self, "marker", "?")
        self._write_state(marker)
    def on_update(self, dt):
        marker = self._read_state()
        if marker.endswith("u"):
            return
        self._write_state(marker + "u")
)");
    const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditTwin");

    const std::filesystem::path stateA = TempStatePath("xora2d_audit_twin_a.txt");
    const std::filesystem::path stateB = TempStatePath("xora2d_audit_twin_b.txt");
    WriteTextFile(stateA, "");
    WriteTextFile(stateB, "");

    harness.AddScriptEntity(handle, "AuditTwin",
                            {Xora2D::ScriptProperty{.Name = "marker",
                                                    .Type = Xora2D::ScriptPropertyType::String,
                                                    .StringValue = "A"},
                             Xora2D::ScriptProperty{.Name = "state_path",
                                                    .Type = Xora2D::ScriptPropertyType::String,
                                                    .StringValue = stateA.string()}});
    harness.AddScriptEntity(handle, "AuditTwin",
                            {Xora2D::ScriptProperty{.Name = "marker",
                                                    .Type = Xora2D::ScriptPropertyType::String,
                                                    .StringValue = "B"},
                             Xora2D::ScriptProperty{.Name = "state_path",
                                                    .Type = Xora2D::ScriptPropertyType::String,
                                                    .StringValue = stateB.string()}});
    harness.Tick(2);
    CHECK(ReadTextFile(stateA) == "Au", "first entity script instance is independent");
    CHECK(ReadTextFile(stateB) == "Bu", "second entity script instance is independent");
}

void TestDisabledScripts(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_disabled.py";
    WriteAssetScript(relative, std::string(R"(
class AuditDisabled:
)") + kStateHelper + R"(
    def on_create(self):
        self._append_state("c")
    def on_update(self, dt):
        self._append_state("u")
    def on_destroy(self):
        self._append_state("d")
)");
    const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditDisabled");
    const std::filesystem::path statePath = TempStatePath("xora2d_audit_disabled.txt");
    WriteTextFile(statePath, "");

    Xora2D::Entity entity = harness.AddScriptEntity(
        handle, "AuditDisabled",
        {Xora2D::ScriptProperty{.Name = "state_path",
                                .Type = Xora2D::ScriptPropertyType::String,
                                .StringValue = statePath.string()}},
        true);
    harness.Tick(1);
    CHECK(ReadTextFile(statePath) == "cu", "enabled script updates");

    auto& component = entity.GetComponent<Xora2D::PythonScriptComponent>();
    component.Enabled = false;
    harness.Tick(2);
    CHECK(ReadTextFile(statePath) == "cud", "disabled script skips update and destroys instance");

    component.Enabled = true;
    harness.Tick(1);
    CHECK(ReadTextFile(statePath) == "cudcu", "re-enabled script runs on_create again");
}

void TestFailureIsolation(ScriptingHarness& harness) {
    const char* badRelative = "Scripts/_audit_bad_update.py";
    const char* goodRelative = "Scripts/_audit_good_update.py";
    WriteAssetScript(badRelative, std::string(R"(
class AuditBad:
)") + kStateHelper + R"(
    def on_update(self, dt):
        raise RuntimeError("intentional test failure")
)");
    WriteAssetScript(goodRelative, std::string(R"(
class AuditGood:
)") + kStateHelper + R"(
    def on_update(self, dt):
        value = self._read_state()
        count = 0 if not value else int(value)
        self._write_state(str(count + 1))
)");
    const Xora2D::AssetHandle bad = RegisterAuditScript(harness.assets, badRelative, "AuditBad");
    const Xora2D::AssetHandle good = RegisterAuditScript(harness.assets, goodRelative, "AuditGood");
    const std::filesystem::path goodState = TempStatePath("xora2d_audit_good_neighbor.txt");
    WriteTextFile(goodState, "");

    harness.AddScriptEntity(bad, "AuditBad");
    harness.AddScriptEntity(
        good, "AuditGood",
        {Xora2D::ScriptProperty{.Name = "state_path",
                                .Type = Xora2D::ScriptPropertyType::String,
                                .StringValue = goodState.string()}});
    harness.Tick(4);
    CHECK(ReadTextFile(goodState) == "4", "neighbor script keeps updating after another script fails");
}

void TestSyntaxErrorAndExtension(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_syntax.py";
    WriteAssetScript(relative, "def incomplete syntax (\n");
    const Xora2D::AssetResult reg =
        harness.assets.RegisterPythonScriptAtRelativePath(relative, "AuditSyntax");
    CHECK(reg.Success, "register syntax-broken script asset metadata");

    auto freshScene = Xora2D::Scene::Create();
    freshScene->SetAssetManager(&harness.assets);
    freshScene->SetPythonScriptEngine(&harness.engine);
    Xora2D::Entity entity = freshScene->CreateEntity("SyntaxEntity");
    auto& component = entity.AddComponent<Xora2D::PythonScriptComponent>();
    component.ScriptAsset = reg.Handle;
    component.ClassName = "AuditSyntax";

    CHECK(!harness.engine.EnsureInstance(*freshScene, entity, component, harness.assets),
          "invalid Python syntax does not create an instance");

    WriteAssetScript("Scripts/_audit_not_py.txt", "print('not a registered script')\n");
    const Xora2D::AssetResult txt =
        harness.assets.RegisterPythonScriptAtRelativePath("Scripts/_audit_not_py.txt", "NotPython");
    CHECK(!txt.Success, "reject unsupported script extension");
}

void TestScriptPropertiesAndBindings(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_bindings.py";
    WriteAssetScript(relative, std::string(R"(
import xora2d
class AuditBindings:
)") + kStateHelper + R"(
    def on_create(self):
        if not self.entity.is_valid():
            self._write_state("invalid")
            return
        name = self.entity.name
        uid = self.entity.uuid
        speed = float(getattr(self, "speed", -1.0))
        flag = bool(getattr(self, "enabled_flag", False))
        label = str(getattr(self, "label", ""))
        self.entity.transform.set_position(4.5, 6.5)
        self._write_state(f"{name}|{uid}|{speed}|{flag}|{label}")
)");
    const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditBindings");
    const std::filesystem::path statePath = TempStatePath("xora2d_audit_bindings.txt");
    WriteTextFile(statePath, "");

    Xora2D::Entity entity = harness.AddScriptEntity(
        handle, "AuditBindings",
        {Xora2D::ScriptProperty{.Name = "state_path",
                                .Type = Xora2D::ScriptPropertyType::String,
                                .StringValue = statePath.string()},
         Xora2D::ScriptProperty{.Name = "speed", .Type = Xora2D::ScriptPropertyType::Number, .NumberValue = 12.5},
         Xora2D::ScriptProperty{.Name = "enabled_flag", .Type = Xora2D::ScriptPropertyType::Boolean, .BooleanValue = true},
         Xora2D::ScriptProperty{.Name = "label", .Type = Xora2D::ScriptPropertyType::String, .StringValue = "demo"}});
    harness.Tick(1);

    const std::string payload = ReadTextFile(statePath);
    CHECK(payload.find("AuditBindingsEntity|") == 0, "entity name binding");
    CHECK(payload.find(entity.GetUUID().ToString()) != std::string::npos, "entity uuid binding as string");
    CHECK(payload.find("|12.5|True|demo") != std::string::npos, "serialized script properties applied");

    const auto& transform = entity.GetComponent<Xora2D::TransformComponent>();
    CHECK(transform.Translation.x == 4.5f && transform.Translation.y == 6.5f, "transform write binding");
}

void TestScriptSerialization(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_serialize.py";
    WriteAssetScript(relative, "class AuditSerialize:\n    def on_update(self, dt):\n        pass\n");
    const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditSerialize");

    auto entity = harness.AddScriptEntity(
        handle, "AuditSerialize",
        {Xora2D::ScriptProperty{.Name = "value", .Type = Xora2D::ScriptPropertyType::Number, .NumberValue = 3.0}},
        true);
    entity.GetComponent<Xora2D::PythonScriptComponent>().Enabled = true;

    const std::filesystem::path scenePath = TempStatePath("xora2d_audit_script_scene.yaml");
    CHECK(Xora2D::SceneSerializer::Serialize(*harness.scene, scenePath).Success, "serialize python script component");

    auto loaded = Xora2D::Scene::Create();
    loaded->SetAssetManager(&harness.assets);
    CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, scenePath, &harness.assets).Success,
          "deserialize python script component");

    const Xora2D::Entity loadedEntity = loaded->FindEntityByName("AuditSerializeEntity");
    CHECK(loadedEntity.IsValid(), "deserialized entity retains python script");
    const auto& script = loadedEntity.GetComponent<Xora2D::PythonScriptComponent>();
    CHECK(script.ScriptAsset == handle, "script asset handle round trip");
    CHECK(script.ClassName == "AuditSerialize", "class name round trip");
    CHECK(script.Properties.size() == 1, "property count round trip");
    CHECK(script.Properties[0].NumberValue == 3.0, "property value round trip");

    const std::filesystem::path badScene = TempStatePath("xora2d_audit_bad_script_scene.yaml");
    WriteTextFile(badScene,
                  R"(Version: 1
Entities:
  - Name: Bad
    ID: "00000000-0000-0000-0000-000000000001"
    Transform: {}
    PythonScriptComponent:
      ScriptAssetHandle: "00000000-0000-0000-0000-000000000002"
      Properties:
        - Name: p
          Type: NotAType
          Value: 1
)");
    auto rejectScene = Xora2D::Scene::Create();
    const std::size_t beforeCount = rejectScene->GetEntityCount();
    CHECK(!Xora2D::SceneSerializer::Deserialize(*rejectScene, badScene, &harness.assets).Success,
          "invalid script property type rejected");
    CHECK(rejectScene->GetEntityCount() == beforeCount, "failed script deserialize is transactional");
}

void TestScriptReload(ScriptingHarness& harness) {
    const char* relative = "Scripts/_audit_reload.py";
    WriteAssetScript(relative, std::string(R"(
class AuditReload:
)") + kStateHelper + R"(
    def on_create(self):
        self._write_state("v1")
)");
    const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditReload");
    const std::filesystem::path statePath = TempStatePath("xora2d_audit_reload.txt");
    WriteTextFile(statePath, "");

    harness.AddScriptEntity(
        handle, "AuditReload",
        {Xora2D::ScriptProperty{.Name = "state_path",
                                .Type = Xora2D::ScriptPropertyType::String,
                                .StringValue = statePath.string()}});
    harness.Tick(1);
    CHECK(ReadTextFile(statePath) == "v1", "initial script version runs");

    WriteAssetScript(relative, std::string(R"(
class AuditReload:
)") + kStateHelper + R"(
    def on_create(self):
        self._write_state("v2")
    def on_update(self, dt):
        self._append_state("u")
)");
    const auto reloadOk = harness.engine.ReloadScript(handle, harness.assets);
    CHECK(reloadOk.Success, "reload valid replacement script");
    harness.Tick(1);
    CHECK(ReadTextFile(statePath) == "v2u", "reloaded script instance recreated");

    WriteAssetScript(relative, "syntax !!!\n");
    const auto reloadBad = harness.engine.ReloadScript(handle, harness.assets);
    CHECK(!reloadBad.Success && reloadBad.KeptPreviousVersion, "reject invalid reload and keep previous script");
    harness.Tick(1);
    CHECK(ReadTextFile(statePath) == "v2uu", "previous valid script continues after failed reload");
}

#endif // XORA2D_SCRIPTING_ENABLED

} // namespace

int RunScriptingTests() {
    const int before = g_scriptingFailures;
    XORA_LOG_INFO("TEST python scripting started");

#if XORA2D_SCRIPTING_ENABLED
    ScriptingHarness harness;
    CHECK(!harness.engine.IsAvailable(), "engine not initialized yet");

    const Xora2D::ScriptLoadResult init = harness.engine.Initialize();
    CHECK(init.Success, "interpreter initializes");
    CHECK(harness.engine.IsAvailable(), "engine available after init");
    CHECK(harness.engine.Initialize().Success, "repeated init tolerated");

    CHECK(harness.Initialize(), "scripting harness initializes scene services");

    const Xora2D::AssetResult bundled =
        harness.assets.RegisterPythonScriptAtRelativePath("Scripts/PlayerController.py", "PlayerController");
    CHECK(bundled.Success, "register bundled player script");
    CHECK(harness.assets.LoadPythonScript(bundled.Handle).Success, "load player script");

    Xora2D::Entity entity = harness.AddScriptEntity(
        bundled.Handle, "PlayerController",
        {Xora2D::ScriptProperty{.Name = "speed", .Type = Xora2D::ScriptPropertyType::Number, .NumberValue = 100.0}});
    harness.Tick(3);
    CHECK(true, "bundled player script on_update tolerated");

    harness.scene->DestroyEntity(entity);
    harness.Tick(1);
    CHECK(true, "entity destroy cleans script instance");

    TestLifecycleAndDestroy(harness);
    TestIndependentInstances(harness);
    TestDisabledScripts(harness);
    TestFailureIsolation(harness);
    TestSyntaxErrorAndExtension(harness);
    TestScriptPropertiesAndBindings(harness);
    TestScriptSerialization(harness);
    TestScriptReload(harness);

    // Scene replacement uses a dedicated harness to avoid cross-test entity accumulation.
    {
        const char* relative = "Scripts/_audit_replace.py";
        WriteAssetScript(relative, std::string(R"(
class AuditReplace:
)") + kStateHelper + R"(
    def on_update(self, dt):
        value = self._read_state()
        count = 0 if not value else int(value)
        self._write_state(str(count + 1))
)");
        const Xora2D::AssetHandle handle = RegisterAuditScript(harness.assets, relative, "AuditReplace");
        const std::filesystem::path statePath = TempStatePath("xora2d_audit_replace.txt");
        WriteTextFile(statePath, "");

        auto source = Xora2D::Scene::Create();
        source->SetAssetManager(&harness.assets);
        source->SetPythonScriptEngine(&harness.engine);
        Xora2D::ScriptServices services{};
        services.Scene = source.get();
        services.Assets = &harness.assets;
        harness.engine.SetServices(services);

        Xora2D::Entity sourceEntity = source->CreateEntity("ReplaceSource");
        auto& script = sourceEntity.AddComponent<Xora2D::PythonScriptComponent>();
        script.ScriptAsset = handle;
        script.ClassName = "AuditReplace";
        script.Properties.push_back(
            Xora2D::ScriptProperty{.Name = "state_path",
                                   .Type = Xora2D::ScriptPropertyType::String,
                                   .StringValue = statePath.string()});

        auto destination = Xora2D::Scene::Create();
        destination->SetAssetManager(&harness.assets);
        destination->SetPythonScriptEngine(&harness.engine);

        source->OnUpdate(0.016f);
        destination->ReplaceContentsFrom(*source);
        services.Scene = destination.get();
        harness.engine.SetServices(services);
        destination->OnUpdate(0.016f);

        CHECK(ReadTextFile(statePath) == "2", "scene replacement recreates script instances safely");
    }

    harness.engine.PrepareShutdown();
    CHECK(harness.engine.IsAvailable(), "interpreter remains until explicit Shutdown");
    harness.engine.Shutdown();
    CHECK(!harness.engine.IsAvailable(), "engine unavailable after shutdown");
    harness.Tick(1);
    CHECK(true, "scene update after interpreter shutdown does not invoke Python");
#else
    Xora2D::PythonScriptEngine engine;
    CHECK(!engine.IsScriptingCompiledIn(), "scripting disabled at compile time");
    const Xora2D::ScriptLoadResult init = engine.Initialize();
    CHECK(!init.Success, "scripting disabled build reports unavailable init");
#endif

    if (g_scriptingFailures == before) {
        XORA_LOG_INFO("TEST python scripting passed");
    } else {
        XORA_LOG_ERROR("TEST python scripting failed");
    }
    return g_scriptingFailures - before;
}