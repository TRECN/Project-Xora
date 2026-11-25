#include "Xora2D/Particles/ParticleEffectAsset.h"

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Core/Logger.h"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Xora2D {
namespace {

// ---- YAML helpers ----

bool ReadFloat(const YAML::Node& node, const char* key, float& out, const std::string& ctx, std::string& error) {
    if (!node[key]) return true; // optional
    try {
        const float v = node[key].as<float>();
        if (!std::isfinite(v)) { error = ctx + ": '" + key + "' is not finite"; return false; }
        out = v;
    } catch (...) { error = ctx + ": '" + key + "' must be a number"; return false; }
    return true;
}

bool ReadUint(const YAML::Node& node, const char* key, std::uint32_t& out, const std::string& ctx, std::string& error) {
    if (!node[key]) return true;
    try { out = node[key].as<std::uint32_t>(); }
    catch (...) { error = ctx + ": '" + key + "' must be a non-negative integer"; return false; }
    return true;
}

bool ReadBool(const YAML::Node& node, const char* key, bool& out, const std::string& ctx, std::string& error) {
    (void)ctx; (void)error;
    if (!node[key]) return true;
    try { out = node[key].as<bool>(); } catch (...) {}
    return true;
}

bool ReadVec2(const YAML::Node& node, const char* key, glm::vec2& out, const std::string& ctx, std::string& error) {
    if (!node[key]) return true;
    const YAML::Node& v = node[key];
    if (!v.IsSequence() || v.size() != 2) { error = ctx + ": '" + key + "' must be [x, y]"; return false; }
    out.x = v[0].as<float>(); out.y = v[1].as<float>();
    return true;
}

bool ReadVec4(const YAML::Node& node, const char* key, glm::vec4& out, const std::string& ctx, std::string& error) {
    if (!node[key]) return true;
    const YAML::Node& v = node[key];
    if (!v.IsSequence() || v.size() != 4) { error = ctx + ": '" + key + "' must be [r, g, b, a]"; return false; }
    out.r = v[0].as<float>(); out.g = v[1].as<float>(); out.b = v[2].as<float>(); out.a = v[3].as<float>();
    return true;
}

bool ReadHandle(const YAML::Node& node, const char* key, AssetHandle& out, const std::string& /*ctx*/, std::string& /*error*/) {
    if (!node[key]) return true;
    if (const auto opt = AssetHandle::FromString(node[key].as<std::string>())) {
        out = *opt;
    }
    return true;
}

bool ReadFloatCurve(const YAML::Node& node, const char* key, FloatCurve& out, const std::string& ctx, std::string& error) {
    if (!node[key]) return true;
    const YAML::Node& seq = node[key];
    if (!seq.IsSequence()) { error = ctx + ": '" + key + "' must be a sequence of {t, v} pairs"; return false; }
    out.Keys.clear();
    for (std::size_t i = 0; i < seq.size(); ++i) {
        FloatCurveKey k;
        k.Time  = seq[i]["t"].as<float>(0.0f);
        k.Value = seq[i]["v"].as<float>(1.0f);
        out.Keys.push_back(k);
    }
    return true;
}

bool ReadColorGradient(const YAML::Node& node, const char* key, ColorGradient& out, const std::string& ctx, std::string& error) {
    if (!node[key]) return true;
    const YAML::Node& seq = node[key];
    if (!seq.IsSequence()) { error = ctx + ": '" + key + "' must be a sequence of {t, c[r,g,b,a]} pairs"; return false; }
    out.Keys.clear();
    for (std::size_t i = 0; i < seq.size(); ++i) {
        ColorGradientKey k;
        k.Time = seq[i]["t"].as<float>(0.0f);
        const YAML::Node& c = seq[i]["c"];
        if (c.IsSequence() && c.size() == 4) {
            k.Color = {c[0].as<float>(), c[1].as<float>(), c[2].as<float>(), c[3].as<float>()};
        }
        out.Keys.push_back(k);
    }
    return true;
}

bool ReadBursts(const YAML::Node& node, std::vector<ParticleBurst>& out, const std::string& ctx, std::string& error) {
    if (!node["Bursts"]) return true;
    const YAML::Node& seq = node["Bursts"];
    if (!seq.IsSequence()) { error = ctx + ": 'Bursts' must be a sequence"; return false; }
    out.clear();
    for (std::size_t i = 0; i < seq.size(); ++i) {
        ParticleBurst b;
        b.Time  = seq[i]["Time"].as<float>(0.0f);
        b.Count = seq[i]["Count"].as<int>(10);
        if (b.Count > 0) out.push_back(b);
    }
    return true;
}

// ---- YAML emit helpers ----

YAML::Node EmitVec2(const glm::vec2& v) {
    YAML::Node n(YAML::NodeType::Sequence);
    n.push_back(v.x); n.push_back(v.y);
    return n;
}

YAML::Node EmitVec4(const glm::vec4& v) {
    YAML::Node n(YAML::NodeType::Sequence);
    n.push_back(v.r); n.push_back(v.g); n.push_back(v.b); n.push_back(v.a);
    return n;
}

YAML::Node EmitFloatCurve(const FloatCurve& curve) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& k : curve.Keys) {
        YAML::Node entry;
        entry["t"] = k.Time;
        entry["v"] = k.Value;
        seq.push_back(entry);
    }
    return seq;
}

YAML::Node EmitColorGradient(const ColorGradient& grad) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& k : grad.Keys) {
        YAML::Node entry;
        entry["t"] = k.Time;
        entry["c"] = EmitVec4(k.Color);
        seq.push_back(entry);
    }
    return seq;
}

YAML::Node EmitBursts(const std::vector<ParticleBurst>& bursts) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& b : bursts) {
        YAML::Node entry;
        entry["Time"]  = b.Time;
        entry["Count"] = b.Count;
        seq.push_back(entry);
    }
    return seq;
}

} // namespace

// ---- ParticleEffectAsset::LoadFromFile ----

AssetResult ParticleEffectAsset::LoadFromFile(const std::filesystem::path& absolutePath,
                                               const std::string& relativePath,
                                               ParticleEffectAsset& output) {
    AssetResult result{};
    output = {};

    if (!std::filesystem::exists(absolutePath)) {
        result.Message = "Particle effect file not found: " + absolutePath.string();
        return result;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(absolutePath.string());
    } catch (const YAML::Exception& ex) {
        result.Message = "YAML parse error in '" + absolutePath.string() + "': " + ex.what();
        return result;
    }

    const std::string ctx = absolutePath.string();

    // Version check
    if (!root["Version"]) {
        result.Message = ctx + ": missing 'Version' field";
        return result;
    }
    const int version = root["Version"].as<int>(0);
    if (version < 1 || version > kParticleEffectFormatVersion) {
        result.Message = ctx + ": unsupported ParticleEffect version " + std::to_string(version);
        return result;
    }

    const YAML::Node& cfg = root["ParticleEffect"];
    if (!cfg || !cfg.IsMap()) {
        result.Message = ctx + ": missing 'ParticleEffect' map";
        return result;
    }

    ParticleEffectConfig c{};
    std::string error;

    bool ok = true;
    ok = ok && ReadUint (cfg, "MaxParticles",      c.MaxParticles,     ctx, error);
    ok = ok && ReadFloat(cfg, "EmissionRate",       c.EmissionRate,     ctx, error);
    ok = ok && ReadBool (cfg, "Looping",            c.Looping,          ctx, error);
    ok = ok && ReadFloat(cfg, "Duration",           c.Duration,         ctx, error);
    ok = ok && ReadFloat(cfg, "StartDelay",         c.StartDelay,       ctx, error);
    ok = ok && ReadFloat(cfg, "LifetimeMin",        c.LifetimeMin,      ctx, error);
    ok = ok && ReadFloat(cfg, "LifetimeMax",        c.LifetimeMax,      ctx, error);
    ok = ok && ReadFloat(cfg, "SpeedMin",           c.SpeedMin,         ctx, error);
    ok = ok && ReadFloat(cfg, "SpeedMax",           c.SpeedMax,         ctx, error);
    ok = ok && ReadVec2 (cfg, "Direction",          c.Direction,        ctx, error);
    ok = ok && ReadFloat(cfg, "SpreadAngle",        c.SpreadAngle,      ctx, error);
    ok = ok && ReadVec2 (cfg, "Gravity",            c.Gravity,          ctx, error);
    ok = ok && ReadFloat(cfg, "SizeBegin",          c.SizeBegin,        ctx, error);
    ok = ok && ReadFloat(cfg, "SizeEnd",            c.SizeEnd,          ctx, error);
    ok = ok && ReadFloatCurve(cfg, "SizeCurve",     c.SizeCurve,        ctx, error);
    ok = ok && ReadFloat(cfg, "RotationBegin",      c.RotationBegin,    ctx, error);
    ok = ok && ReadFloat(cfg, "RotationEnd",        c.RotationEnd,      ctx, error);
    ok = ok && ReadFloat(cfg, "AngularVelocityMin", c.AngularVelocityMin, ctx, error);
    ok = ok && ReadFloat(cfg, "AngularVelocityMax", c.AngularVelocityMax, ctx, error);
    ok = ok && ReadVec4 (cfg, "ColorBegin",         c.ColorBegin,       ctx, error);
    ok = ok && ReadVec4 (cfg, "ColorEnd",           c.ColorEnd,         ctx, error);
    ok = ok && ReadColorGradient(cfg, "ColorOverLifetime", c.ColorOverLifetime, ctx, error);
    ok = ok && ReadFloatCurve(cfg, "OpacityCurve",  c.OpacityCurve,     ctx, error);
    ok = ok && ReadHandle(cfg, "TextureAsset",       c.TextureAsset,     ctx, error);
    ok = ok && ReadBool (cfg, "WorldSpace",          c.WorldSpace,       ctx, error);
    ok = ok && ReadBursts(cfg, c.Bursts,                                  ctx, error);

    if (cfg["RandomSeed"]) {
        c.RandomSeed = cfg["RandomSeed"].as<std::uint64_t>(12345);
    }

    if (!ok) {
        result.Message = error;
        return result;
    }

    // Clamp MaxParticles to a sane maximum to prevent accidental giant allocations.
    c.MaxParticles = std::min(c.MaxParticles, static_cast<std::uint32_t>(100000));
    if (c.MaxParticles == 0) c.MaxParticles = 1;

    output.m_valid        = true;
    output.m_relativePath = relativePath;
    output.m_config       = std::move(c);

    result.Success = true;
    return result;
}

// ---- ParticleEffectAsset::SaveToFile ----

AssetResult ParticleEffectAsset::SaveToFile(const std::filesystem::path& absolutePath,
                                             const ParticleEffectConfig& config) {
    AssetResult result{};

    std::error_code ec;
    std::filesystem::create_directories(absolutePath.parent_path(), ec);
    if (ec) {
        result.Message = "Could not create parent directory: " + ec.message();
        return result;
    }

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Version" << YAML::Value << kParticleEffectFormatVersion;
    out << YAML::Key << "ParticleEffect" << YAML::Value << YAML::BeginMap;

    out << YAML::Key << "MaxParticles"      << YAML::Value << config.MaxParticles;
    out << YAML::Key << "EmissionRate"      << YAML::Value << config.EmissionRate;
    out << YAML::Key << "Looping"           << YAML::Value << config.Looping;
    out << YAML::Key << "Duration"          << YAML::Value << config.Duration;
    out << YAML::Key << "StartDelay"        << YAML::Value << config.StartDelay;
    out << YAML::Key << "RandomSeed"        << YAML::Value << config.RandomSeed;
    out << YAML::Key << "Bursts"            << YAML::Value << EmitBursts(config.Bursts);
    out << YAML::Key << "LifetimeMin"       << YAML::Value << config.LifetimeMin;
    out << YAML::Key << "LifetimeMax"       << YAML::Value << config.LifetimeMax;
    out << YAML::Key << "SpeedMin"          << YAML::Value << config.SpeedMin;
    out << YAML::Key << "SpeedMax"          << YAML::Value << config.SpeedMax;
    out << YAML::Key << "Direction"         << YAML::Value << EmitVec2(config.Direction);
    out << YAML::Key << "SpreadAngle"       << YAML::Value << config.SpreadAngle;
    out << YAML::Key << "Gravity"           << YAML::Value << EmitVec2(config.Gravity);
    out << YAML::Key << "SizeBegin"         << YAML::Value << config.SizeBegin;
    out << YAML::Key << "SizeEnd"           << YAML::Value << config.SizeEnd;
    out << YAML::Key << "SizeCurve"         << YAML::Value << EmitFloatCurve(config.SizeCurve);
    out << YAML::Key << "RotationBegin"     << YAML::Value << config.RotationBegin;
    out << YAML::Key << "RotationEnd"       << YAML::Value << config.RotationEnd;
    out << YAML::Key << "AngularVelocityMin" << YAML::Value << config.AngularVelocityMin;
    out << YAML::Key << "AngularVelocityMax" << YAML::Value << config.AngularVelocityMax;
    out << YAML::Key << "ColorBegin"        << YAML::Value << EmitVec4(config.ColorBegin);
    out << YAML::Key << "ColorEnd"          << YAML::Value << EmitVec4(config.ColorEnd);
    out << YAML::Key << "ColorOverLifetime" << YAML::Value << EmitColorGradient(config.ColorOverLifetime);
    out << YAML::Key << "OpacityCurve"      << YAML::Value << EmitFloatCurve(config.OpacityCurve);
    out << YAML::Key << "TextureAsset"      << YAML::Value << config.TextureAsset.ToString();
    out << YAML::Key << "WorldSpace"        << YAML::Value << config.WorldSpace;

    out << YAML::EndMap; // ParticleEffect
    out << YAML::EndMap; // root

    std::ofstream file(absolutePath);
    if (!file.is_open()) {
        result.Message = "Could not open file for writing: " + absolutePath.string();
        return result;
    }
    file << out.c_str();
    if (!file) {
        result.Message = "Write error: " + absolutePath.string();
        return result;
    }

    result.Success = true;
    return result;
}

} // namespace Xora2D
