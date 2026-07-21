#include <gtest/gtest.h>

#include <engine/assets/inochi/inochi_puppet.h>

#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

// Seam 1 of the Inochi runtime track: load a .inp / .inx (TRNSRTS) puppet into
// the in-memory Puppet. A hand-built container covers the parse contract with no
// fixture; the real Aka.inp (CC-BY-4.0, seagetch) covers the true schema when the
// fixture is present.

namespace
{
    namespace ic = wz::engine::assets::inochi;

    void put_be_u32(std::vector<std::uint8_t>& b, std::uint32_t v)
    {
        b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
        b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }

    // A minimal uncompressed 32-bit TGA (top-left origin), distinct per texel so a
    // transposed width/height would fail rather than pass by coincidence.
    std::vector<std::uint8_t> make_tga(int w, int h)
    {
        std::vector<std::uint8_t> t;
        t.push_back(0); t.push_back(0); t.push_back(2); // uncompressed true-colour
        t.insert(t.end(), 5, 0);                        // colour-map spec
        t.push_back(0); t.push_back(0);                 // x origin
        t.push_back(0); t.push_back(0);                 // y origin
        t.push_back(static_cast<std::uint8_t>(w & 0xFF));
        t.push_back(static_cast<std::uint8_t>((w >> 8) & 0xFF));
        t.push_back(static_cast<std::uint8_t>(h & 0xFF));
        t.push_back(static_cast<std::uint8_t>((h >> 8) & 0xFF));
        t.push_back(32);   // bpp
        t.push_back(0x28); // top-left origin + 8 alpha bits
        for (int i = 0; i < w * h; ++i) {
            t.push_back(static_cast<std::uint8_t>((i * 7) & 0xFF));
            t.push_back(static_cast<std::uint8_t>((i * 13) & 0xFF));
            t.push_back(static_cast<std::uint8_t>((i * 29) & 0xFF));
            t.push_back(255);
        }
        return t;
    }

    std::vector<std::uint8_t> build_inp(const std::string& json,
                                        const std::vector<std::uint8_t>& tga)
    {
        std::vector<std::uint8_t> b;
        const char magic[8] = { 'T', 'R', 'N', 'S', 'R', 'T', 'S', 0 };
        b.insert(b.end(), magic, magic + 8);
        put_be_u32(b, static_cast<std::uint32_t>(json.size()));
        b.insert(b.end(), json.begin(), json.end());
        const char tex[8] = { 'T', 'E', 'X', '_', 'S', 'E', 'C', 'T' };
        b.insert(b.end(), tex, tex + 8);
        put_be_u32(b, 1);                                       // one texture
        put_be_u32(b, static_cast<std::uint32_t>(tga.size()));  // length
        b.push_back(1);                                         // encoding: TGA
        b.insert(b.end(), tga.begin(), tga.end());
        return b;
    }
}

TEST(InochiLoad, ParsesHandBuiltContainer)
{
    const std::string json = R"JSON({
      "meta": { "rigger": "test", "licenseURL": "cc-by" },
      "nodes": {
        "uuid": 1, "name": "root", "type": "Node",
        "transform": { "trans": [0,0,0], "rot": [0,0,0], "scale": [1,1] },
        "children": [
          { "uuid": 2, "name": "p", "type": "Part", "blend_mode": "Multiply", "opacity": 0.5,
            "transform": { "trans": [3,4,0], "rot": [0,0,0], "scale": [1,1] },
            "mesh": { "verts": [0,0,1,0,1,1,0,1], "uvs": [0,0,1,0,1,1,0,1], "indices": [0,1,2,0,2,3] },
            "textures": [0] }
        ]
      },
      "param": [
        { "uuid": 3, "name": "P", "is_vec2": false, "min": [0,0], "max": [1,1], "defaults": [0,0],
          "axis_points": [[0,1],[0]],
          "bindings": [
            { "node": 2, "param_name": "transform.t.x", "interpolate_mode": "Linear",
              "isSet": [[true],[true]], "values": [[10],[20]] }
          ] }
      ]
    })JSON";

    const std::vector<std::uint8_t> buf = build_inp(json, make_tga(2, 2));

    ic::Puppet p;
    std::string err;
    ASSERT_TRUE(ic::load_puppet(buf.data(), buf.size(), p, &err)) << err;

    EXPECT_EQ(p.meta.rigger, "test");

    ASSERT_EQ(p.nodes.size(), 2u);
    EXPECT_EQ(p.nodes[0].kind, ic::NodeKind::Node);
    EXPECT_EQ(p.nodes[0].name, "root");
    ASSERT_EQ(p.nodes[0].children.size(), 1u);
    EXPECT_EQ(p.nodes[0].children[0], 1u);

    const ic::Node& part = p.nodes[1];
    EXPECT_EQ(part.kind, ic::NodeKind::Part);
    EXPECT_EQ(part.blend_mode, ic::BlendMode::Multiply);
    EXPECT_FLOAT_EQ(part.opacity, 0.5f);
    EXPECT_FLOAT_EQ(part.transform.trans[0], 3.0f);
    EXPECT_EQ(part.mesh.vertex_count(), 4u);
    ASSERT_EQ(part.mesh.indices.size(), 6u);
    ASSERT_EQ(part.textures.size(), 1u);
    EXPECT_EQ(part.textures[0], 0u);

    ASSERT_EQ(p.parameters.size(), 1u);
    const ic::Parameter& par = p.parameters[0];
    EXPECT_EQ(par.name, "P");
    EXPECT_EQ(par.nx(), 2u);
    EXPECT_EQ(par.ny(), 1u);
    ASSERT_EQ(par.bindings.size(), 1u);
    EXPECT_EQ(par.bindings[0].target, ic::BindTarget::TransTX);
    ASSERT_EQ(par.bindings[0].scalar_values.size(), 2u);
    EXPECT_FLOAT_EQ(par.bindings[0].scalar_values[0], 10.0f);
    EXPECT_FLOAT_EQ(par.bindings[0].scalar_values[1], 20.0f);

    ASSERT_EQ(p.textures.size(), 1u);
    EXPECT_EQ(p.textures[0].width, 2u);
    EXPECT_EQ(p.textures[0].height, 2u);
    EXPECT_EQ(p.textures[0].rgba.size(), 2u * 2u * 4u);
}

TEST(InochiLoad, ParsesRealAkaPuppet)
{
    const std::string path = std::string(WZ_TEST_FIXTURE_DIR) + "/inochi/Aka.inp";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        GTEST_SKIP() << "fixture not present: " << path;
    }
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 12u);

    ic::Puppet p;
    std::string err;
    ASSERT_TRUE(ic::load_puppet(bytes.data(), bytes.size(), p, &err)) << err;

    EXPECT_EQ(p.meta.rigger, "seagetch");
    EXPECT_NE(p.meta.license_url.find("creativecommons"), std::string::npos);

    std::size_t parts = 0, composites = 0, physics = 0;
    for (const auto& n : p.nodes) {
        if (n.kind == ic::NodeKind::Part) ++parts;
        else if (n.kind == ic::NodeKind::Composite) ++composites;
        else if (n.kind == ic::NodeKind::SimplePhysics) ++physics;
    }
    EXPECT_EQ(p.nodes.size(), 100u);
    EXPECT_EQ(parts, 76u);
    EXPECT_EQ(composites, 3u);
    EXPECT_EQ(physics, 19u);
    EXPECT_EQ(p.parameters.size(), 34u);
    EXPECT_EQ(p.textures.size(), 3u);  // .inp atlases the per-part textures into 3 pages

    // A Part carries a real decoded mesh; a deform binding carries per-vertex offsets.
    bool any_mesh = false, any_deform = false;
    for (const auto& n : p.nodes)
        if (n.kind == ic::NodeKind::Part && n.mesh.vertex_count() > 0 && !n.mesh.indices.empty())
            any_mesh = true;
    for (const auto& par : p.parameters)
        for (const auto& b : par.bindings)
            if (b.target == ic::BindTarget::Deform && !b.deform_values.empty())
                any_deform = true;
    EXPECT_TRUE(any_mesh);
    EXPECT_TRUE(any_deform);

    // Every atlas page decoded to RGBA.
    for (const auto& t : p.textures) {
        EXPECT_GT(t.width, 0u);
        EXPECT_GT(t.height, 0u);
        EXPECT_EQ(t.rgba.size(), static_cast<std::size_t>(t.width) * t.height * 4u);
    }
}
