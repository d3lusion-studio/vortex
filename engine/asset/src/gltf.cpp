#include "vortex/asset/gltf.hpp"

#include "vortex/core/json.hpp"
#include "vortex/core/log.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace vortex::assets {

namespace {

// glTF component types, straight from the spec's numbering.
constexpr u32 kByte          = 5120;
constexpr u32 kUnsignedByte  = 5121;
constexpr u32 kShort         = 5122;
constexpr u32 kUnsignedShort = 5123;
constexpr u32 kUnsignedInt   = 5125;
constexpr u32 kFloat         = 5126;

std::vector<u8> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size = in.tellg();
    in.seekg(0);
    std::vector<u8> bytes(static_cast<usize>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::string directoryOf(const std::string& path) {
    const usize slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

u32 componentCount(std::string_view type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2")   return 2;
    if (type == "VEC3")   return 3;
    if (type == "VEC4")   return 4;
    if (type == "MAT4")   return 16;
    return 0;
}

u32 componentSize(u32 componentType) {
    switch (componentType) {
        case kByte:
        case kUnsignedByte:  return 1;
        case kShort:
        case kUnsignedShort: return 2;
        case kUnsignedInt:
        case kFloat:         return 4;
        default:             return 0;
    }
}

// Everything the file needs to be read through: an accessor names a view of a buffer, and a
// view may be strided (interleaved with other attributes), so nothing here may assume the
// data it wants is contiguous.
class Reader {
public:
    Reader(const json::Value& gltf, std::vector<u8> buffer)
        : m_gltf(gltf), m_buffer(std::move(buffer)) {}

    // Read accessor `index` as floats, whatever it is stored as. Integer component types are
    // normalised (divided by their max) when the accessor says so — which is how glTF packs
    // weights and vertex colours into half the space.
    [[nodiscard]] std::vector<f32> readFloats(u32 index, u32& outComponents) const {
        const json::Value& acc = m_gltf["accessors"][index];
        const u32 comps = componentCount(acc["type"].asString());
        const u32 ctype = acc["componentType"].asU32();
        const u32 count = acc["count"].asU32();
        outComponents   = comps;

        std::vector<f32> out(static_cast<usize>(count) * comps, 0.0f);
        const bool normalized = acc["normalized"].asBool(false);

        forEachElement(acc, comps, ctype, count, [&](usize element, const u8* src) {
            for (u32 c = 0; c < comps; ++c) {
                const u8* p = src + static_cast<usize>(c) * componentSize(ctype);
                f32 v = 0.0f;
                switch (ctype) {
                    case kFloat:         std::memcpy(&v, p, 4); break;
                    case kUnsignedByte:  v = static_cast<f32>(*p) / (normalized ? 255.0f : 1.0f); break;
                    case kByte:          v = static_cast<f32>(*reinterpret_cast<const i8*>(p)) /
                                             (normalized ? 127.0f : 1.0f); break;
                    case kUnsignedShort: { u16 t; std::memcpy(&t, p, 2);
                                           v = static_cast<f32>(t) / (normalized ? 65535.0f : 1.0f); break; }
                    case kShort:         { i16 t; std::memcpy(&t, p, 2);
                                           v = static_cast<f32>(t) / (normalized ? 32767.0f : 1.0f); break; }
                    case kUnsignedInt:   { u32 t; std::memcpy(&t, p, 4); v = static_cast<f32>(t); break; }
                    default: break;
                }
                out[element * comps + c] = v;
            }
        });
        return out;
    }

    // Read an accessor as unsigned integers. Indices and joint IDs — never normalise these.
    [[nodiscard]] std::vector<u32> readUints(u32 index) const {
        const json::Value& acc = m_gltf["accessors"][index];
        const u32 comps = componentCount(acc["type"].asString());
        const u32 ctype = acc["componentType"].asU32();
        const u32 count = acc["count"].asU32();

        std::vector<u32> out(static_cast<usize>(count) * comps, 0u);
        forEachElement(acc, comps, ctype, count, [&](usize element, const u8* src) {
            for (u32 c = 0; c < comps; ++c) {
                const u8* p = src + static_cast<usize>(c) * componentSize(ctype);
                u32 v = 0;
                switch (ctype) {
                    case kUnsignedByte:  v = *p; break;
                    case kUnsignedShort: { u16 t; std::memcpy(&t, p, 2); v = t; break; }
                    case kUnsignedInt:   std::memcpy(&v, p, 4); break;
                    default: break;
                }
                out[element * comps + c] = v;
            }
        });
        return out;
    }

    [[nodiscard]] std::vector<Mat4> readMatrices(u32 index) const {
        u32 comps = 0;
        const std::vector<f32> raw = readFloats(index, comps);
        if (comps != 16) return {};

        std::vector<Mat4> out(raw.size() / 16);
        for (usize i = 0; i < out.size(); ++i)
            std::memcpy(out[i].m, raw.data() + i * 16, 16 * sizeof(f32));   // glTF is column-major, as we are
        return out;
    }

private:
    template <typename Fn>
    void forEachElement(const json::Value& acc, u32 comps, u32 ctype, u32 count, Fn fn) const {
        if (comps == 0 || componentSize(ctype) == 0) return;
        if (!acc.contains("bufferView")) return;   // sparse/zero-filled accessor

        const json::Value& view = m_gltf["bufferViews"][acc["bufferView"].asU32()];
        const usize viewOffset = view["byteOffset"].asU32(0);
        const usize accOffset  = acc["byteOffset"].asU32(0);
        const usize elemSize   = static_cast<usize>(comps) * componentSize(ctype);
        // A stride of zero means tightly packed. Anything else is an interleaved buffer, and
        // reading it as if it were packed is how you get a mesh made of noise.
        const usize stride = view["byteStride"].asU32(0) != 0 ? view["byteStride"].asU32(0)
                                                              : elemSize;

        for (u32 i = 0; i < count; ++i) {
            const usize offset = viewOffset + accOffset + static_cast<usize>(i) * stride;
            if (offset + elemSize > m_buffer.size()) return;   // truncated file: stop, do not read past
            fn(i, m_buffer.data() + offset);
        }
    }

    const json::Value& m_gltf;
    std::vector<u8>    m_buffer;
};

Mat4 nodeMatrix(const json::Value& node) {
    if (node.contains("matrix")) {
        const json::Value& m = node["matrix"];
        Mat4 out;
        for (usize i = 0; i < 16 && i < m.size(); ++i) out.m[i] = m[i].asF32();
        return out;
    }

    anim::Transform t;
    if (node.contains("translation")) {
        const json::Value& v = node["translation"];
        t.translation = {v[0].asF32(), v[1].asF32(), v[2].asF32()};
    }
    if (node.contains("rotation")) {
        const json::Value& v = node["rotation"];   // glTF stores xyzw
        t.rotation = Quat{v[0].asF32(), v[1].asF32(), v[2].asF32(), v[3].asF32()}.normalized();
    }
    if (node.contains("scale")) {
        const json::Value& v = node["scale"];
        t.scale = {v[0].asF32(), v[1].asF32(), v[2].asF32()};
    }
    return t.matrix();
}

anim::Transform nodeTransform(const json::Value& node) {
    anim::Transform t;
    if (node.contains("translation")) {
        const json::Value& v = node["translation"];
        t.translation = {v[0].asF32(), v[1].asF32(), v[2].asF32()};
    }
    if (node.contains("rotation")) {
        const json::Value& v = node["rotation"];
        t.rotation = Quat{v[0].asF32(), v[1].asF32(), v[2].asF32(), v[3].asF32()}.normalized();
    }
    if (node.contains("scale")) {
        const json::Value& v = node["scale"];
        t.scale = {v[0].asF32(), v[1].asF32(), v[2].asF32()};
    }
    return t;
}

std::string textureUri(const json::Value& gltf, const json::Value& texRef) {
    if (!texRef.contains("index")) return {};
    const u32 tex = texRef["index"].asU32();
    if (tex >= gltf["textures"].size()) return {};
    const json::Value& texture = gltf["textures"][tex];
    if (!texture.contains("source")) return {};
    const u32 img = texture["source"].asU32();
    if (img >= gltf["images"].size()) return {};
    return gltf["images"][img]["uri"].asString();
}

} // namespace

const anim::Clip* GltfModel::findClip(std::string_view name) const {
    for (const anim::Clip& c : animations)
        if (c.name == name) return &c;
    return nullptr;
}

GltfModel loadGltf(const char* path, std::string* error) {
    GltfModel model;
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return GltfModel{};
    };

    const std::string file = path;
    const std::vector<u8> text = readFile(file);
    if (text.empty()) return fail("cannot open " + file);

    std::string jsonError;
    const json::Value gltf = json::parse(
        std::string_view(reinterpret_cast<const char*>(text.data()), text.size()), &jsonError);
    if (!gltf.isObject()) return fail("not valid JSON: " + jsonError);

    // --- the binary blob every accessor eventually points into ---
    if (gltf["buffers"].size() == 0) return fail("no buffers");
    const std::string uri = gltf["buffers"][0]["uri"].asString();
    if (uri.empty() || uri.rfind("data:", 0) == 0)
        return fail("only external .bin buffers are supported (no embedded base64, no .glb)");

    const std::string dir = directoryOf(file);
    std::vector<u8> bin = readFile(dir + uri);
    if (bin.empty()) return fail("cannot open buffer " + dir + uri);

    const Reader reader(gltf, std::move(bin));

    // --- materials ---
    for (const json::Value& m : gltf["materials"].items()) {
        GltfMaterial mat;
        mat.name        = m["name"].asString();
        mat.doubleSided = m["doubleSided"].asBool(false);
        mat.blend       = m["alphaMode"].asString("OPAQUE") == "BLEND";

        const json::Value& pbr = m["pbrMetallicRoughness"];
        if (pbr.isObject()) {
            if (pbr.contains("baseColorFactor")) {
                const json::Value& c = pbr["baseColorFactor"];
                mat.baseColor = {c[0].asF32(1), c[1].asF32(1), c[2].asF32(1), c[3].asF32(1)};
            }
            mat.metallic  = pbr["metallicFactor"].asF32(1.0f);
            mat.roughness = pbr["roughnessFactor"].asF32(1.0f);
            mat.baseColorTexture = textureUri(gltf, pbr["baseColorTexture"]);
            mat.metallicRoughnessTexture = textureUri(gltf, pbr["metallicRoughnessTexture"]);
        }
        mat.normalTexture   = textureUri(gltf, m["normalTexture"]);
        mat.emissiveTexture = textureUri(gltf, m["emissiveTexture"]);
        if (m.contains("emissiveFactor")) {
            const json::Value& e = m["emissiveFactor"];
            mat.emissive = {e[0].asF32(), e[1].asF32(), e[2].asF32()};
        }
        model.materials.push_back(std::move(mat));
    }

    // --- who is whose parent ---
    const usize nodeCount = gltf["nodes"].size();
    std::vector<i32> parentOf(nodeCount, -1);
    for (usize n = 0; n < nodeCount; ++n)
        for (const json::Value& c : gltf["nodes"][static_cast<u32>(n)]["children"].items())
            parentOf[c.asU32()] = static_cast<i32>(n);

    // --- the skinned mesh node, and the skeleton it points at ---
    i32 meshNode = -1;
    for (usize n = 0; n < nodeCount; ++n)
        if (gltf["nodes"][static_cast<u32>(n)].contains("mesh")) { meshNode = static_cast<i32>(n); break; }
    if (meshNode < 0) return fail("no node carries a mesh");

    const json::Value& mnode = gltf["nodes"][static_cast<u32>(meshNode)];

    // The mesh node's transform, with every ancestor's folded in.
    {
        Mat4 world = Mat4::identity();
        for (i32 n = meshNode; n >= 0; n = parentOf[static_cast<usize>(n)])
            world = nodeMatrix(gltf["nodes"][static_cast<u32>(n)]) * world;
        model.rootTransform = world;
    }

    // jointOfNode[node] = its index in the skin, or -1. The vertex data indexes joints in the
    // skin's own order, so that order is the one the skeleton must keep.
    std::vector<i32> jointOfNode(nodeCount, -1);

    if (mnode.contains("skin") && gltf["skins"].size() > 0) {
        const json::Value& skin = gltf["skins"][mnode["skin"].asU32()];
        const json::Value& jointNodes = skin["joints"];

        std::vector<u32> nodes;
        for (const json::Value& j : jointNodes.items()) nodes.push_back(j.asU32());

        std::vector<Mat4> inverseBinds;
        if (skin.contains("inverseBindMatrices"))
            inverseBinds = reader.readMatrices(skin["inverseBindMatrices"].asU32());

        for (usize i = 0; i < nodes.size(); ++i)
            jointOfNode[nodes[i]] = static_cast<i32>(i);

        model.skeleton.joints.resize(nodes.size());
        for (usize i = 0; i < nodes.size(); ++i) {
            const json::Value& jn = gltf["nodes"][nodes[i]];
            anim::Joint& joint = model.skeleton.joints[i];
            joint.name     = jn["name"].asString();
            joint.bindPose = nodeTransform(jn);
            if (i < inverseBinds.size()) joint.inverseBind = inverseBinds[i];

            // A joint's parent is the nearest ancestor that is also a joint. Walking up rather
            // than reading the node's immediate parent matters: a rig often has plain nodes
            // (a control, an empty) sitting between two bones.
            joint.parent = -1;
            for (i32 p = parentOf[nodes[i]]; p >= 0; p = parentOf[static_cast<usize>(p)])
                if (jointOfNode[static_cast<usize>(p)] >= 0) {
                    joint.parent = jointOfNode[static_cast<usize>(p)];
                    break;
                }
        }

        // The runtime computes globals in one forward pass, which is only correct if every
        // joint appears after its parent. glTF does not promise that, so check — and say so
        // loudly rather than rendering a subtly broken pose.
        for (usize i = 0; i < model.skeleton.joints.size(); ++i)
            if (model.skeleton.joints[i].parent >= static_cast<i32>(i))
                return fail("skin joints are not in parents-first order; reordering is not "
                            "implemented (it would mean remapping every vertex's joint indices)");
    }

    // --- geometry ---
    const json::Value& mesh = gltf["meshes"][mnode["mesh"].asU32()];
    for (const json::Value& prim : mesh["primitives"].items()) {
        const json::Value& attrs = prim["attributes"];
        if (!attrs.contains("POSITION")) continue;

        GltfPrimitive out;
        out.material = prim.contains("material") ? static_cast<i32>(prim["material"].asU32()) : -1;

        u32 comps = 0;
        const std::vector<f32> positions = reader.readFloats(attrs["POSITION"].asU32(), comps);
        const usize vertexCount = positions.size() / 3;

        std::vector<f32> normals, uvs, weights, colors;
        std::vector<u32> joints;
        if (attrs.contains("NORMAL"))     normals = reader.readFloats(attrs["NORMAL"].asU32(), comps);
        if (attrs.contains("TEXCOORD_0")) uvs     = reader.readFloats(attrs["TEXCOORD_0"].asU32(), comps);
        if (attrs.contains("WEIGHTS_0"))  weights = reader.readFloats(attrs["WEIGHTS_0"].asU32(), comps);
        if (attrs.contains("JOINTS_0"))   joints  = reader.readUints(attrs["JOINTS_0"].asU32());

        u32 colorComps = 0;
        if (attrs.contains("COLOR_0")) colors = reader.readFloats(attrs["COLOR_0"].asU32(), colorComps);

        out.mesh.vertices.resize(vertexCount);
        for (usize v = 0; v < vertexCount; ++v) {
            renderer::MeshVertex& mv = out.mesh.vertices[v];
            mv.position = {positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]};
            if (normals.size() >= (v + 1) * 3)
                mv.normal = {normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]};
            if (uvs.size() >= (v + 1) * 2)
                mv.uv = {uvs[v * 2], uvs[v * 2 + 1]};
            if (colors.size() >= (v + 1) * colorComps && colorComps >= 3)
                mv.color = {colors[v * colorComps], colors[v * colorComps + 1],
                            colors[v * colorComps + 2],
                            colorComps == 4 ? colors[v * colorComps + 3] : 1.0f};

            if (joints.size() >= (v + 1) * 4 && weights.size() >= (v + 1) * 4) {
                f32 sum = 0.0f;
                for (u32 c = 0; c < 4; ++c) {
                    // The renderer holds joint indices in a byte, so a rig with more than 255
                    // bones would silently alias — clamp and let it be visibly wrong instead.
                    const u32 j = joints[v * 4 + c];
                    mv.joints[c] = static_cast<u8>(std::min(j, 255u));
                    sum += weights[v * 4 + c];
                }
                // Renormalise. Exporters quantise weights, and four that add to 0.998 dim the
                // whole vertex by a fifth of a percent — visible as a shimmer across a mesh.
                const f32 inv = sum > 0.0001f ? 1.0f / sum : 0.0f;
                mv.weights = {weights[v * 4] * inv, weights[v * 4 + 1] * inv,
                              weights[v * 4 + 2] * inv, weights[v * 4 + 3] * inv};
            }
        }

        if (prim.contains("indices")) {
            out.mesh.indices = reader.readUints(prim["indices"].asU32());
        } else {
            out.mesh.indices.resize(vertexCount);
            for (usize i = 0; i < vertexCount; ++i) out.mesh.indices[i] = static_cast<u32>(i);
        }

        out.mesh.computeTangents();
        model.primitives.push_back(std::move(out));
    }

    // --- animations ---
    for (const json::Value& a : gltf["animations"].items()) {
        anim::Clip clip;
        clip.name = a["name"].asString();
        clip.tracks.resize(model.skeleton.size());

        for (const json::Value& ch : a["channels"].items()) {
            const json::Value& target = ch["target"];
            if (!target.contains("node")) continue;

            const i32 joint = jointOfNode[target["node"].asU32()];
            if (joint < 0) continue;   // animates something that is not a bone: not our problem

            const json::Value& sampler = a["samplers"][ch["sampler"].asU32()];
            const std::string interpName = sampler["interpolation"].asString("LINEAR");
            if (interpName == "CUBICSPLINE") {
                VORTEX_WARN("glTF", "clip '%s' uses CUBICSPLINE, which is not implemented; "
                                    "that channel is skipped", clip.name.c_str());
                continue;
            }
            const auto interp = interpName == "STEP" ? anim::Interpolation::Step
                                                     : anim::Interpolation::Linear;

            u32 dummy = 0;
            std::vector<f32> times  = reader.readFloats(sampler["input"].asU32(), dummy);
            std::vector<f32> values = reader.readFloats(sampler["output"].asU32(), dummy);
            if (times.empty()) continue;

            for (const f32 t : times) clip.duration = std::max(clip.duration, t);

            anim::JointTrack& track = clip.tracks[static_cast<usize>(joint)];
            const std::string path = target["path"].asString();

            if (path == "translation" && values.size() >= times.size() * 3) {
                track.translation.times  = times;
                track.translation.interp = interp;
                track.translation.values.resize(times.size());
                for (usize i = 0; i < times.size(); ++i)
                    track.translation.values[i] = {values[i * 3], values[i * 3 + 1], values[i * 3 + 2]};
            } else if (path == "rotation" && values.size() >= times.size() * 4) {
                track.rotation.times  = times;
                track.rotation.interp = interp;
                track.rotation.values.resize(times.size());
                for (usize i = 0; i < times.size(); ++i)
                    track.rotation.values[i] = Quat{values[i * 4], values[i * 4 + 1],
                                                    values[i * 4 + 2], values[i * 4 + 3]}.normalized();
            } else if (path == "scale" && values.size() >= times.size() * 3) {
                track.scale.times  = times;
                track.scale.interp = interp;
                track.scale.values.resize(times.size());
                for (usize i = 0; i < times.size(); ++i)
                    track.scale.values[i] = {values[i * 3], values[i * 3 + 1], values[i * 3 + 2]};
            }
        }
        model.animations.push_back(std::move(clip));
    }

    return model;
}

}
