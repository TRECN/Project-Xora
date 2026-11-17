#include "Xora2D/Input/InputMappingSerializer.h"

#include "Xora2D/Core/Logger.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

namespace Xora2D {
namespace {

InputMappingSerializeResult Failure(std::string message) {
    return InputMappingSerializeResult{false, std::move(message)};
}

InputMappingSerializeResult Success() {
    return InputMappingSerializeResult{true, {}};
}

std::string BindingTypeToString(InputBinding::Type type) {
    switch (type) {
    case InputBinding::Type::Key:
        return "Key";
    case InputBinding::Type::MouseButton:
        return "MouseButton";
    default:
        return "Unknown";
    }
}

std::optional<InputBinding::Type> BindingTypeFromString(const std::string& text) {
    if (text == "Key") {
        return InputBinding::Type::Key;
    }
    if (text == "MouseButton") {
        return InputBinding::Type::MouseButton;
    }
    return std::nullopt;
}

} // namespace

InputMappingSerializeResult InputMappingSerializer::Load(InputMapping& mapping,
                                                           const std::filesystem::path& filepath) {
    try {
    if (!std::filesystem::exists(filepath)) {
        return Failure("Input mapping file not found");
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(filepath.string());
    } catch (const YAML::Exception& exception) {
        const char* what = exception.what();
        return Failure(std::string("Malformed input mapping YAML: ") + (what != nullptr ? what : "unknown"));
    }

    if (!root["Version"]) {
        return Failure("Input mapping missing Version");
    }
    const int version = root["Version"].as<int>();
    if (version != kFormatVersion) {
        return Failure("Unsupported input mapping version");
    }

    InputMapping parsed{};
    const YAML::Node actions = root["Actions"];
    if (!actions || !actions.IsMap()) {
        return Failure("Actions must be a map");
    }

    for (const auto& entry : actions) {
        const std::string actionName = entry.first.as<std::string>();
        if (actionName.empty()) {
            return Failure("Empty action name in input mapping");
        }
        const YAML::Node bindingsNode = entry.second;
        if (!bindingsNode || !bindingsNode.IsSequence()) {
            return Failure("Action bindings must be a sequence");
        }

        for (const YAML::Node& bindingNode : bindingsNode) {
            if (!bindingNode["Type"] || !bindingNode["Code"]) {
                return Failure("Binding missing Type or Code");
            }
            const std::optional<InputBinding::Type> type =
                BindingTypeFromString(bindingNode["Type"].as<std::string>());
            if (!type.has_value()) {
                return Failure("Invalid binding type");
            }
            const std::string code = bindingNode["Code"].as<std::string>();

            if (*type == InputBinding::Type::Key) {
                const std::optional<Key> key = KeyFromName(code);
                if (!key.has_value() || !IsValidKey(*key)) {
                    return Failure("Invalid key code: " + code);
                }
                if (!parsed.BindKey(actionName, *key)) {
                    return Failure("Failed to bind key for action: " + actionName);
                }
            } else {
                const std::optional<MouseButton> button = MouseButtonFromName(code);
                if (!button.has_value() || !IsValidMouseButton(*button)) {
                    return Failure("Invalid mouse button code: " + code);
                }
                if (!parsed.BindMouseButton(actionName, *button)) {
                    return Failure("Failed to bind mouse button for action: " + actionName);
                }
            }
        }
    }

    mapping = std::move(parsed);
    return Success();
    } catch (const std::exception& exception) {
        const char* what = exception.what();
        return Failure(std::string("Input mapping load error: ") + (what != nullptr ? what : "unknown"));
    } catch (...) {
        return Failure("Input mapping load error: unknown exception");
    }
}

InputMappingSerializeResult InputMappingSerializer::Save(const InputMapping& mapping,
                                                         const std::filesystem::path& filepath) {
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "Version" << YAML::Value << kFormatVersion;
    emitter << YAML::Key << "Actions" << YAML::Value << YAML::BeginMap;

    for (const auto& [action, bindings] : mapping.m_actions) {
        emitter << YAML::Key << action << YAML::Value << YAML::BeginSeq;
        for (const InputBinding& binding : bindings) {
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "Type" << YAML::Value << BindingTypeToString(binding.BindingType);
            if (binding.BindingType == InputBinding::Type::Key) {
                emitter << YAML::Key << "Code" << YAML::Value << KeyToName(binding.KeyCode);
            } else {
                emitter << YAML::Key << "Code" << YAML::Value << MouseButtonToName(binding.MouseButtonCode);
            }
            emitter << YAML::EndMap;
        }
        emitter << YAML::EndSeq;
    }

    emitter << YAML::EndMap;
    emitter << YAML::EndMap;

    std::ofstream file(filepath);
    if (!file) {
        return Failure("Failed to open input mapping file for writing");
    }
    const char* emitted = emitter.c_str();
    if (emitted == nullptr) {
        return Failure("YAML emission failed");
    }
    file << emitted;
    if (!file) {
        return Failure("Failed to write input mapping file");
    }
    return Success();
}

} // namespace Xora2D