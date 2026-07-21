#pragma once

// file: engine/assets/inochi/inochi_puppet.h
//
// In-memory Inochi2D puppet: the parsed payload of a .inp / .inx (TRNSRTS)
// container -- the node tree, parameters + bindings, meshes, and decoded
// textures. This is renderer-agnostic DATA; the deform, composite, and render
// seams consume it. Route C (native reimplementation on wozzits RHI) --- see
// the inochi-runtime-track. The container framing (magic, big-endian JSON
// length, TEX_SECT, EXT_SECT) is documented at docs.inochi2d.com/spec/inp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets::inochi
{
    // Inochi transform: 3D translate (z = layer depth), 3D euler rotate (rot.z is
    // the 2D spin; rot.x/rot.y are still driven by some bindings), 2D scale.
    struct Transform
    {
        std::array<float, 3> trans{ 0.0f, 0.0f, 0.0f };
        std::array<float, 3> rot{ 0.0f, 0.0f, 0.0f };
        std::array<float, 2> scale{ 1.0f, 1.0f };
    };

    enum class NodeKind : std::uint8_t
    {
        Node,           // transform-only grouping
        Part,           // drawable textured mesh
        Composite,      // renders children to an offscreen target, composites the group
        SimplePhysics,  // spring/pendulum driver -> writes a parameter
        Unknown,        // forward-compat: an unrecognized type (kept, not drawn)
    };

    // The blend set Inochi authors. Normal/Multiply/Screen/Additive map to
    // fixed-function; ClipToLower/SliceFromLower are compositing masks; the
    // destination-reading modes (Overlay, SoftLight, ...) need the offscreen
    // backdrop (seam 6). Aka uses only Normal/Multiply/ClipToLower.
    enum class BlendMode : std::uint8_t
    {
        Normal, Multiply, Screen, Overlay, Darken, Lighten,
        ColorDodge, LinearDodge, AddGlow, ColorBurn, HardLight, SoftLight,
        Subtract, Difference, Exclusion, Inverse, DestinationIn,
        ClipToLower, SliceFromLower,
        Unknown,
    };

    enum class MaskMode : std::uint8_t { Mask, DodgeMask, Unknown };

    struct MaskBinding
    {
        std::uint32_t source_uuid = 0;   // node whose geometry masks this part
        MaskMode mode = MaskMode::Mask;
    };

    // A 2D triangle mesh in the part's local pixel space. verts/uvs are
    // interleaved (x,y). Indices are widened to u32 to match the engine's pull
    // path even though Inochi's per-part meshes are small (Aka: <=239 verts).
    struct Mesh
    {
        std::vector<float> verts;         // 2*N, local pixel space
        std::vector<float> uvs;           // 2*N
        std::vector<std::uint32_t> indices;  // 3*T
        std::array<float, 2> origin{ 0.0f, 0.0f };

        [[nodiscard]] std::size_t vertex_count() const noexcept
        {
            return verts.size() / 2;
        }
    };

    struct Node
    {
        std::uint32_t uuid = 0;
        std::string name;
        NodeKind kind = NodeKind::Node;
        bool enabled = true;
        bool lock_to_root = false;
        float zsort = 0.0f;
        Transform transform;
        std::vector<std::size_t> children;  // indices into Puppet::nodes

        // --- Part ---
        Mesh mesh;
        std::vector<std::uint32_t> textures;  // indices into Puppet::textures (albedo[, emissive, bump])
        BlendMode blend_mode = BlendMode::Normal;
        float opacity = 1.0f;
        std::array<float, 3> tint{ 1.0f, 1.0f, 1.0f };
        std::array<float, 3> screen_tint{ 0.0f, 0.0f, 0.0f };
        float mask_threshold = 0.5f;
        std::vector<MaskBinding> masks;

        // --- SimplePhysics ---
        std::uint32_t physics_param = 0;   // parameter uuid this driver writes
        std::string physics_model;         // "Pendulum" | "SpringPendulum"
        std::string physics_map_mode;      // "AngleLength" | "XY"
        float gravity = 1.0f;
        float length = 0.0f;
        float frequency = 1.0f;
        float angle_damping = 0.5f;
        float length_damping = 0.5f;
        std::array<float, 2> output_scale{ 1.0f, 1.0f };
    };

    // The property a binding drives on its target node. Scalar targets take one
    // float per grid cell; Deform takes a per-vertex (dx,dy) offset array per cell.
    enum class BindTarget : std::uint8_t
    {
        TransTX, TransTY, TransTZ,
        RotX, RotY, RotZ,
        ScaleX, ScaleY,
        ZSort,
        Deform,
        Unknown,
    };

    enum class InterpolateMode : std::uint8_t { Nearest, Linear, Cubic, Unknown };

    // One binding: a parameter drives one property of one node over the param's
    // axis grid. Grid is laid out [x_index * ny + y_index], matching
    // Parameter::axis_points. Scalar targets fill `scalar_values` (nx*ny);
    // Deform fills `deform_values` (nx*ny cells, each a 2*vertex_count array of
    // (dx,dy) pairs). `is_set` marks authored cells vs interpolated ones.
    struct Binding
    {
        std::uint32_t node_uuid = 0;
        BindTarget target = BindTarget::Unknown;
        std::string raw_target;             // original "param_name" (forward-compat)
        InterpolateMode interpolate = InterpolateMode::Linear;
        std::vector<std::uint8_t> is_set;   // nx*ny
        std::vector<float> scalar_values;               // nx*ny (scalar targets)
        std::vector<std::vector<float>> deform_values;  // nx*ny (deform target)
    };

    struct Parameter
    {
        std::uint32_t uuid = 0;
        std::string name;
        bool is_vec2 = false;
        std::array<float, 2> min{ 0.0f, 0.0f };
        std::array<float, 2> max{ 1.0f, 1.0f };
        std::array<float, 2> defaults{ 0.0f, 0.0f };
        std::array<std::vector<float>, 2> axis_points;  // [0]=x keys, [1]=y keys (1D => y has one key)
        std::vector<Binding> bindings;

        [[nodiscard]] std::size_t nx() const noexcept { return axis_points[0].size(); }
        [[nodiscard]] std::size_t ny() const noexcept { return axis_points[1].size(); }
    };

    // A texture blob from TEX_SECT. PNG/TGA are decoded to RGBA8 via the shared
    // image_decode (seam 1); BC7 is kept encoded for a GPU decode path later.
    struct Texture
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint8_t encoding = 0;      // 0=PNG, 1=TGA, 2=BC7 (as stored)
        std::vector<std::uint8_t> rgba; // width*height*4 when decoded; else empty
        std::vector<std::uint8_t> encoded;  // original bytes when not decoded (e.g. BC7)
    };

    struct Meta
    {
        std::string name;
        std::string version;
        std::string rigger;
        std::string artist;
        std::string copyright;
        std::string license_url;
        std::string contact;
    };

    struct Puppet
    {
        Meta meta;
        std::vector<Node> nodes;          // flattened tree; nodes[0] is the root
        std::vector<Parameter> parameters;
        std::vector<Texture> textures;
        // physics world settings + automation are parsed in a later seam.
    };

    struct LoadOptions
    {
        // Decode PNG/TGA texture blobs to RGBA now (false keeps them encoded).
        bool decode_textures = true;
    };

    // Parse a .inp / .inx (TRNSRTS) container from raw bytes. On success fills
    // `out` and returns true; on failure returns false and, if provided, sets
    // `error`. Reads magic + big-endian JSON payload + TEX_SECT; EXT_SECT (an
    // .inx's editor data) is skipped.
    [[nodiscard]] bool load_puppet(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        Puppet& out,
        std::string* error = nullptr,
        const LoadOptions& options = {});
}
