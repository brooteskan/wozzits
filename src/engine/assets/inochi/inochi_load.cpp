// src/engine/assets/inochi/inochi_load.cpp
//
// Parse a .inp / .inx (TRNSRTS) container into an in-memory Puppet. Container
// framing (big-endian): 'TRNSRTS\0' magic, u32 JSON length, JSON payload, then a
// 'TEX_SECT' block (u32 count + per-texture: u32 length, 1-byte encoding, data).
// An .inx additionally carries an 'EXT_SECT' with Inochi Creator's editor data,
// which the runtime skips. The payload JSON is parsed with the engine's wz::json;
// PNG/TGA textures decode via the shared image_decode (BC7 is kept encoded).

#include <engine/assets/inochi/inochi_puppet.h>

#include <engine/assets/texture/image_decode.h>
#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace wz::engine::assets::inochi
{
    namespace
    {
        namespace j = wz::json;

        constexpr std::uint8_t kMagic[8] = { 'T', 'R', 'N', 'S', 'R', 'T', 'S', 0 };
        constexpr char kTexSect[8] = { 'T', 'E', 'X', '_', 'S', 'E', 'C', 'T' };

        std::uint32_t read_be_u32(const std::uint8_t* p) noexcept
        {
            return (static_cast<std::uint32_t>(p[0]) << 24)
                 | (static_cast<std::uint32_t>(p[1]) << 16)
                 | (static_cast<std::uint32_t>(p[2]) << 8)
                 |  static_cast<std::uint32_t>(p[3]);
        }

        bool fail(std::string* error, std::string message)
        {
            if (error) *error = std::move(message);
            return false;
        }

        // ---- thin typed accessors over wz::json ----

        const j::JSONValue* member(const j::JSONValue& obj, std::string_view key)
        {
            return j::find_member(obj, key);
        }

        float num(const j::JSONValue& obj, std::string_view key, float fallback)
        {
            const auto v = j::read_number(obj, key);
            return v ? static_cast<float>(*v) : fallback;
        }

        std::uint32_t uint_or(const j::JSONValue& obj, std::string_view key, std::uint32_t fallback)
        {
            const auto v = j::read_uint(obj, key);
            return v ? *v : fallback;
        }

        bool boolean(const j::JSONValue& obj, std::string_view key, bool fallback)
        {
            const auto v = j::read_bool(obj, key);
            return v ? *v : fallback;
        }

        std::string str(const j::JSONValue& obj, std::string_view key)
        {
            const auto v = j::read_string(obj, key);
            return v ? std::string{ *v } : std::string{};
        }

        bool read_float2(const j::JSONValue& obj, std::string_view key, float out[2])
        {
            const auto* v = j::find_member(obj, key);
            if (!v || v->kind != j::JSONValueKind::Array || v->array_values.size() != 2)
                return false;
            for (int i = 0; i < 2; ++i) {
                const auto& e = v->array_values[i];
                if (!e || e->kind != j::JSONValueKind::Number) return false;
                out[i] = static_cast<float>(e->number_value);
            }
            return true;
        }

        void read_number_array(const j::JSONValue* arr, std::vector<float>& out)
        {
            if (!arr || arr->kind != j::JSONValueKind::Array) return;
            out.reserve(arr->array_values.size());
            for (const auto& e : arr->array_values) {
                if (e && e->kind == j::JSONValueKind::Number)
                    out.push_back(static_cast<float>(e->number_value));
            }
        }

        void read_index_array(const j::JSONValue* arr, std::vector<std::uint32_t>& out)
        {
            if (!arr || arr->kind != j::JSONValueKind::Array) return;
            out.reserve(arr->array_values.size());
            for (const auto& e : arr->array_values) {
                if (e && e->kind == j::JSONValueKind::Number && e->number_value >= 0.0)
                    out.push_back(static_cast<std::uint32_t>(e->number_value));
            }
        }

        // ---- enum string maps ----

        NodeKind node_kind(std::string_view t)
        {
            if (t == "Node") return NodeKind::Node;
            if (t == "Part") return NodeKind::Part;
            if (t == "Composite") return NodeKind::Composite;
            if (t == "SimplePhysics") return NodeKind::SimplePhysics;
            return NodeKind::Unknown;
        }

        BlendMode blend_mode(std::string_view s)
        {
            if (s == "Normal") return BlendMode::Normal;
            if (s == "Multiply") return BlendMode::Multiply;
            if (s == "Screen") return BlendMode::Screen;
            if (s == "Overlay") return BlendMode::Overlay;
            if (s == "Darken") return BlendMode::Darken;
            if (s == "Lighten") return BlendMode::Lighten;
            if (s == "ColorDodge") return BlendMode::ColorDodge;
            if (s == "LinearDodge") return BlendMode::LinearDodge;
            if (s == "AddGlow") return BlendMode::AddGlow;
            if (s == "ColorBurn") return BlendMode::ColorBurn;
            if (s == "HardLight") return BlendMode::HardLight;
            if (s == "SoftLight") return BlendMode::SoftLight;
            if (s == "Subtract") return BlendMode::Subtract;
            if (s == "Difference") return BlendMode::Difference;
            if (s == "Exclusion") return BlendMode::Exclusion;
            if (s == "Inverse") return BlendMode::Inverse;
            if (s == "DestinationIn") return BlendMode::DestinationIn;
            if (s == "ClipToLower") return BlendMode::ClipToLower;
            if (s == "SliceFromLower") return BlendMode::SliceFromLower;
            return BlendMode::Unknown;
        }

        MaskMode mask_mode(std::string_view s)
        {
            if (s == "Mask") return MaskMode::Mask;
            if (s == "DodgeMask") return MaskMode::DodgeMask;
            return MaskMode::Unknown;
        }

        BindTarget bind_target(std::string_view s)
        {
            if (s == "transform.t.x") return BindTarget::TransTX;
            if (s == "transform.t.y") return BindTarget::TransTY;
            if (s == "transform.t.z") return BindTarget::TransTZ;
            if (s == "transform.r.x") return BindTarget::RotX;
            if (s == "transform.r.y") return BindTarget::RotY;
            if (s == "transform.r.z") return BindTarget::RotZ;
            if (s == "transform.s.x") return BindTarget::ScaleX;
            if (s == "transform.s.y") return BindTarget::ScaleY;
            if (s == "zSort") return BindTarget::ZSort;
            if (s == "deform") return BindTarget::Deform;
            return BindTarget::Unknown;
        }

        InterpolateMode interp_mode(std::string_view s)
        {
            if (s == "Nearest") return InterpolateMode::Nearest;
            if (s == "Linear") return InterpolateMode::Linear;
            if (s == "Cubic") return InterpolateMode::Cubic;
            return InterpolateMode::Unknown;
        }

        // ---- node / mesh / mask ----

        Transform read_transform(const j::JSONValue& jnode)
        {
            Transform t;
            const auto* tv = member(jnode, "transform");
            if (!tv || tv->kind != j::JSONValueKind::Object) return t;
            float f3[3];
            if (j::read_float3(*tv, "trans", f3)) t.trans = { f3[0], f3[1], f3[2] };
            if (j::read_float3(*tv, "rot", f3))   t.rot   = { f3[0], f3[1], f3[2] };
            float f2[2];
            if (read_float2(*tv, "scale", f2))     t.scale = { f2[0], f2[1] };
            return t;
        }

        void read_mesh(const j::JSONValue& jnode, Mesh& mesh)
        {
            const auto* jm = member(jnode, "mesh");
            if (!jm || jm->kind != j::JSONValueKind::Object) return;
            read_number_array(member(*jm, "verts"), mesh.verts);
            read_number_array(member(*jm, "uvs"), mesh.uvs);
            read_index_array(member(*jm, "indices"), mesh.indices);
            float f2[2];
            if (read_float2(*jm, "origin", f2)) mesh.origin = { f2[0], f2[1] };
        }

        void read_masks(const j::JSONValue& jnode, std::vector<MaskBinding>& masks)
        {
            const auto* jmk = member(jnode, "masks");
            if (!jmk || jmk->kind != j::JSONValueKind::Array) return;
            for (const auto& e : jmk->array_values) {
                if (!e || e->kind != j::JSONValueKind::Object) continue;
                MaskBinding mb;
                mb.source_uuid = uint_or(*e, "source", 0);
                mb.mode = mask_mode(str(*e, "mode"));
                masks.push_back(mb);
            }
        }

        // Recursively parse a node subtree, appending to out.nodes. Returns the
        // index of this node; nodes[0] ends up being the root. out.nodes may
        // reallocate during child recursion, so this re-accesses by index rather
        // than holding a reference across the recursive calls.
        std::size_t parse_node(const j::JSONValue& jnode, Puppet& out)
        {
            Node n;
            n.uuid = uint_or(jnode, "uuid", 0);
            n.name = str(jnode, "name");
            n.kind = node_kind(str(jnode, "type"));
            n.enabled = boolean(jnode, "enabled", true);
            n.lock_to_root = boolean(jnode, "lockToRoot", false);
            n.zsort = num(jnode, "zsort", 0.0f);
            n.transform = read_transform(jnode);

            if (n.kind == NodeKind::Part || n.kind == NodeKind::Composite) {
                n.blend_mode = blend_mode(str(jnode, "blend_mode"));
                n.opacity = num(jnode, "opacity", 1.0f);
                float f3[3];
                if (j::read_float3(jnode, "tint", f3)) n.tint = { f3[0], f3[1], f3[2] };
                if (j::read_float3(jnode, "screenTint", f3)) n.screen_tint = { f3[0], f3[1], f3[2] };
                n.mask_threshold = num(jnode, "mask_threshold", 0.5f);
            }
            if (n.kind == NodeKind::Part) {
                read_mesh(jnode, n.mesh);
                read_index_array(member(jnode, "textures"), n.textures);
                read_masks(jnode, n.masks);
            }
            else if (n.kind == NodeKind::SimplePhysics) {
                n.physics_param = uint_or(jnode, "param", 0);
                n.physics_model = str(jnode, "model_type");
                n.physics_map_mode = str(jnode, "map_mode");
                n.gravity = num(jnode, "gravity", 1.0f);
                n.length = num(jnode, "length", 0.0f);
                n.frequency = num(jnode, "frequency", 1.0f);
                n.angle_damping = num(jnode, "angle_damping", 0.5f);
                n.length_damping = num(jnode, "length_damping", 0.5f);
                float f2[2];
                if (read_float2(jnode, "output_scale", f2)) n.output_scale = { f2[0], f2[1] };
            }

            const std::size_t idx = out.nodes.size();
            out.nodes.push_back(std::move(n));

            const auto* kids = member(jnode, "children");
            if (kids && kids->kind == j::JSONValueKind::Array) {
                std::vector<std::size_t> child_indices;
                child_indices.reserve(kids->array_values.size());
                for (const auto& c : kids->array_values) {
                    if (c && c->kind == j::JSONValueKind::Object)
                        child_indices.push_back(parse_node(*c, out));
                }
                out.nodes[idx].children = std::move(child_indices);
            }
            return idx;
        }

        // ---- parameters ----

        void read_axis_points(const j::JSONValue& jparam, Parameter& p)
        {
            const auto* ap = member(jparam, "axis_points");
            if (ap && ap->kind == j::JSONValueKind::Array) {
                for (std::size_t axis = 0; axis < ap->array_values.size() && axis < 2; ++axis)
                    read_number_array(ap->array_values[axis].get(), p.axis_points[axis]);
            }
            // A 1D parameter omits the y axis; give it a single key so grids are
            // well-formed (nx*ny cells, ny == 1).
            if (p.axis_points[0].empty()) p.axis_points[0].push_back(0.0f);
            if (p.axis_points[1].empty()) p.axis_points[1].push_back(0.0f);
        }

        // Grid is stored [x_index][y_index] to match axis_points; flattened to
        // xi*ny + yi. Scalar cells are Numbers; deform cells are arrays of [dx,dy]
        // pairs (flattened per cell to dx0,dy0,dx1,dy1,...).
        void read_binding(const j::JSONValue& jb, std::size_t nx, std::size_t ny, Binding& b)
        {
            b.node_uuid = uint_or(jb, "node", 0);
            b.raw_target = str(jb, "param_name");
            b.target = bind_target(b.raw_target);
            b.interpolate = interp_mode(str(jb, "interpolate_mode"));

            const std::size_t cells = nx * ny;

            b.is_set.assign(cells, 0);
            if (const auto* js = member(jb, "isSet"); js && js->kind == j::JSONValueKind::Array) {
                for (std::size_t xi = 0; xi < js->array_values.size() && xi < nx; ++xi) {
                    const auto& col = js->array_values[xi];
                    if (!col || col->kind != j::JSONValueKind::Array) continue;
                    for (std::size_t yi = 0; yi < col->array_values.size() && yi < ny; ++yi) {
                        const auto& cell = col->array_values[yi];
                        if (cell && cell->kind == j::JSONValueKind::Bool && cell->bool_value)
                            b.is_set[xi * ny + yi] = 1;
                    }
                }
            }

            const auto* jv = member(jb, "values");
            if (!jv || jv->kind != j::JSONValueKind::Array) return;

            if (b.target == BindTarget::Deform) {
                b.deform_values.assign(cells, {});
                for (std::size_t xi = 0; xi < jv->array_values.size() && xi < nx; ++xi) {
                    const auto& col = jv->array_values[xi];
                    if (!col || col->kind != j::JSONValueKind::Array) continue;
                    for (std::size_t yi = 0; yi < col->array_values.size() && yi < ny; ++yi) {
                        const auto& cell = col->array_values[yi]; // array of [dx,dy]
                        if (!cell || cell->kind != j::JSONValueKind::Array) continue;
                        auto& dst = b.deform_values[xi * ny + yi];
                        dst.reserve(cell->array_values.size() * 2);
                        for (const auto& pr : cell->array_values) {
                            if (pr && pr->kind == j::JSONValueKind::Array && pr->array_values.size() >= 2) {
                                dst.push_back(static_cast<float>(pr->array_values[0]->number_value));
                                dst.push_back(static_cast<float>(pr->array_values[1]->number_value));
                            }
                        }
                    }
                }
            }
            else {
                b.scalar_values.assign(cells, 0.0f);
                for (std::size_t xi = 0; xi < jv->array_values.size() && xi < nx; ++xi) {
                    const auto& col = jv->array_values[xi];
                    if (!col || col->kind != j::JSONValueKind::Array) continue;
                    for (std::size_t yi = 0; yi < col->array_values.size() && yi < ny; ++yi) {
                        const auto& cell = col->array_values[yi];
                        if (cell && cell->kind == j::JSONValueKind::Number)
                            b.scalar_values[xi * ny + yi] = static_cast<float>(cell->number_value);
                    }
                }
            }
        }

        void read_parameters(const j::JSONValue& root, Puppet& out)
        {
            const auto* jp = member(root, "param");
            if (!jp || jp->kind != j::JSONValueKind::Array) return;
            out.parameters.reserve(jp->array_values.size());
            for (const auto& e : jp->array_values) {
                if (!e || e->kind != j::JSONValueKind::Object) continue;
                Parameter p;
                p.uuid = uint_or(*e, "uuid", 0);
                p.name = str(*e, "name");
                p.is_vec2 = boolean(*e, "is_vec2", false);
                float f2[2];
                if (read_float2(*e, "min", f2)) p.min = { f2[0], f2[1] };
                if (read_float2(*e, "max", f2)) p.max = { f2[0], f2[1] };
                if (read_float2(*e, "defaults", f2)) p.defaults = { f2[0], f2[1] };
                read_axis_points(*e, p);
                const std::size_t nx = p.nx();
                const std::size_t ny = p.ny();
                if (const auto* jb = member(*e, "bindings"); jb && jb->kind == j::JSONValueKind::Array) {
                    p.bindings.reserve(jb->array_values.size());
                    for (const auto& bv : jb->array_values) {
                        if (!bv || bv->kind != j::JSONValueKind::Object) continue;
                        Binding binding;
                        read_binding(*bv, nx, ny, binding);
                        p.bindings.push_back(std::move(binding));
                    }
                }
                out.parameters.push_back(std::move(p));
            }
        }

        void read_meta(const j::JSONValue& root, Meta& meta)
        {
            const auto* m = member(root, "meta");
            if (!m || m->kind != j::JSONValueKind::Object) return;
            meta.name = str(*m, "name");
            meta.version = str(*m, "version");
            meta.rigger = str(*m, "rigger");
            meta.artist = str(*m, "artist");
            meta.copyright = str(*m, "copyright");
            meta.license_url = str(*m, "licenseURL");
            meta.contact = str(*m, "contact");
        }

        bool read_textures(const std::uint8_t* bytes, std::size_t count, std::size_t off,
                           bool decode, std::vector<Texture>& out, std::string* error)
        {
            if (off + 12 > count) return fail(error, "truncated before TEX_SECT");
            if (std::memcmp(bytes + off, kTexSect, 8) != 0)
                return fail(error, "missing TEX_SECT marker");
            const std::uint32_t n = read_be_u32(bytes + off + 8);
            std::size_t p = off + 12;
            out.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                if (p + 5 > count) return fail(error, "truncated texture header");
                const std::uint32_t len = read_be_u32(bytes + p);
                const std::uint8_t enc = bytes[p + 4];
                const std::size_t data_off = p + 5;
                if (data_off + len > count) return fail(error, "truncated texture data");

                Texture tex;
                tex.encoding = enc;
                const std::uint8_t* data = bytes + data_off;
                const bool decodable = (enc == 0 || enc == 1); // PNG or TGA
                if (decode && decodable) {
                    DecodedImage img =
                        decode_image_rgba8(std::span<const std::uint8_t>(data, len));
                    if (img.ok) {
                        tex.width = img.width;
                        tex.height = img.height;
                        tex.rgba = std::move(img.rgba8);
                    }
                    else {
                        tex.encoded.assign(data, data + len);
                    }
                }
                else {
                    tex.encoded.assign(data, data + len); // BC7 or decode disabled
                }
                out.push_back(std::move(tex));
                p = data_off + len;
            }
            return true;
        }
    } // namespace

    bool load_puppet(const std::uint8_t* bytes, std::size_t byte_count, Puppet& out,
                     std::string* error, const LoadOptions& options)
    {
        out = Puppet{};

        if (!bytes || byte_count < 12)
            return fail(error, "buffer too small for an INP header");
        if (std::memcmp(bytes, kMagic, 8) != 0)
            return fail(error, "bad magic (expected 'TRNSRTS')");

        const std::uint32_t json_len = read_be_u32(bytes + 8);
        if (12 + static_cast<std::size_t>(json_len) > byte_count)
            return fail(error, "JSON payload length exceeds buffer");

        j::JSONParseResult parsed = j::parse_json_bytes(bytes + 12, json_len);
        if (!parsed.ok || !parsed.document.root)
            return fail(error, "JSON payload parse failed: " + parsed.error.message);

        const j::JSONValue& root = *parsed.document.root;
        if (root.kind != j::JSONValueKind::Object)
            return fail(error, "JSON root is not an object");

        read_meta(root, out.meta);

        if (const auto* jnodes = member(root, "nodes");
            jnodes && jnodes->kind == j::JSONValueKind::Object) {
            parse_node(*jnodes, out);
        }
        if (out.nodes.empty())
            return fail(error, "puppet has no root node");

        read_parameters(root, out);

        const std::size_t tex_off = 12 + static_cast<std::size_t>(json_len);
        if (!read_textures(bytes, byte_count, tex_off, options.decode_textures,
                           out.textures, error))
            return false;

        return true;
    }
}
