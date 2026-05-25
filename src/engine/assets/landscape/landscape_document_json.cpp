// engine/assets/landscape/landscape_document_json.cpp
//
// JSON serialization for LandscapeDocument using yyjson.

#include <engine/assets/landscape/landscape_document.h>

#include <file/filesystem.h>

// yyjson — used directly for both reading and writing.
#include "../../../external/json/yyjson/yyjson.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace wz::engine::assets
{

// ═══════════════════════════════════════════════════════════════════════════
// JSON write helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

yyjson_mut_val* jstr(yyjson_mut_doc* doc, const std::string& s)
{
    return yyjson_mut_str(doc, s.c_str());
}

yyjson_mut_val* jstr(yyjson_mut_doc* doc, const char* s)
{
    return yyjson_mut_str(doc, s);
}

void set_str(yyjson_mut_doc* doc, yyjson_mut_val* obj,
             const char* key, const std::string& val)
{
    if (!val.empty())
        yyjson_mut_obj_add_str(doc, obj, key, val.c_str());
}

void set_float(yyjson_mut_doc* doc, yyjson_mut_val* obj,
               const char* key, float val)
{
    yyjson_mut_obj_add_real(doc, obj, key, static_cast<double>(val));
}

void set_int(yyjson_mut_doc* doc, yyjson_mut_val* obj,
             const char* key, int val)
{
    yyjson_mut_obj_add_int(doc, obj, key, val);
}

void set_uint(yyjson_mut_doc* doc, yyjson_mut_val* obj,
              const char* key, uint32_t val)
{
    yyjson_mut_obj_add_uint(doc, obj, key, val);
}

void set_bool(yyjson_mut_doc* doc, yyjson_mut_val* obj,
              const char* key, bool val)
{
    yyjson_mut_obj_add_bool(doc, obj, key, val);
}

void set_float3(yyjson_mut_doc* doc, yyjson_mut_val* obj,
                const char* key, const float v[3])
{
    auto* arr = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_real(doc, arr, v[0]);
    yyjson_mut_arr_add_real(doc, arr, v[1]);
    yyjson_mut_arr_add_real(doc, arr, v[2]);
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}


// ── Write sub-objects ────────────────────────────────────────────────────

yyjson_mut_val* write_field_source(
    yyjson_mut_doc* doc, const LandscapeFieldSource& fs)
{
    auto* obj = yyjson_mut_obj(doc);
    set_str(doc, obj, "name",    fs.name);
    set_str(doc, obj, "path",    fs.path);
    set_str(doc, obj, "format",  fs.format);
    set_str(doc, obj, "channel", fs.channel);
    if (fs.width  > 0) set_uint(doc, obj, "width",  fs.width);
    if (fs.height > 0) set_uint(doc, obj, "height", fs.height);
    return obj;
}

yyjson_mut_val* write_field_map(
    yyjson_mut_doc* doc, const FieldMap& fm)
{
    auto* obj = yyjson_mut_obj(doc);
    set_str(doc, obj,  "field_name", fm.field_name);
    set_bool(doc, obj, "normalize",  fm.normalize);
    set_float(doc, obj, "scale",     fm.scale);
    set_float(doc, obj, "bias",      fm.bias);
    return obj;
}

yyjson_mut_val* write_geometry_desc(
    yyjson_mut_doc* doc,
    const GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc& d)
{
    auto* obj = yyjson_mut_obj(doc);
    set_float(doc, obj, "height_scale",    d.height_scale);
    set_float(doc, obj, "step_x",          d.step_x);
    set_float(doc, obj, "step_z",          d.step_z);
    set_float(doc, obj, "overlap_factor",  d.overlap_factor);
    set_float(doc, obj, "thickness",       d.thickness);
    set_uint(doc, obj,  "subsample_step",  d.subsample_step);
    set_float(doc, obj, "opacity",         d.opacity);
    set_float(doc, obj, "flat_luminance",  d.flat_luminance);
    set_float(doc, obj, "steep_luminance", d.steep_luminance);
    set_float(doc, obj, "max_slope_stretch", d.max_slope_stretch);
    set_bool(doc, obj,  "normal_smoothing_enabled",
             d.normal_smoothing_enabled);
    set_uint(doc, obj,  "normal_smoothing_radius_cells",
             d.normal_smoothing_radius_cells);
    set_float(doc, obj, "normal_smoothing_sigma_cells",
              d.normal_smoothing_sigma_cells);
    return obj;
}

yyjson_mut_val* write_normal_config(
    yyjson_mut_doc* doc, const NormalFieldConfig& n)
{
    auto* obj = yyjson_mut_obj(doc);
    set_int(doc, obj,  "source",        static_cast<int>(n.source));
    set_str(doc, obj,  "field_x",       n.field_x);
    set_str(doc, obj,  "field_y",       n.field_y);
    set_str(doc, obj,  "field_z",       n.field_z);
    set_int(doc, obj,  "encoded_range", static_cast<int>(n.encoded_range));
    set_int(doc, obj,  "up_axis",       static_cast<int>(n.up_axis));
    set_bool(doc, obj, "flip_x",        n.flip_x);
    set_bool(doc, obj, "flip_z",        n.flip_z);
    set_float(doc, obj, "blend",        n.blend);
    return obj;
}

yyjson_mut_val* write_curvature_config(
    yyjson_mut_doc* doc, const CurvatureFieldConfig& c)
{
    auto* obj = yyjson_mut_obj(doc);
    set_str(doc, obj,   "field_name",       c.field_name);
    set_bool(doc, obj,  "normalize",        c.normalize);
    set_float(doc, obj, "scale",            c.scale);
    set_float(doc, obj, "bias",             c.bias);
    set_float(doc, obj, "radius_influence", c.radius_influence);
    set_float(doc, obj, "min_radius_mul",   c.min_radius_mul);
    set_float(doc, obj, "opacity_influence", c.opacity_influence);
    set_float(doc, obj, "opacity_boost",    c.opacity_boost);
    return obj;
}

yyjson_mut_val* write_peaks_config(
    yyjson_mut_doc* doc, const PeaksFieldConfig& p)
{
    auto* obj = yyjson_mut_obj(doc);
    set_str(doc, obj,   "field_name",             p.field_name);
    set_bool(doc, obj,  "normalize",              p.normalize);
    set_float(doc, obj, "scale",                  p.scale);
    set_float(doc, obj, "bias",                   p.bias);
    set_float(doc, obj, "color_influence",         p.color_influence);
    set_float(doc, obj, "color_brightness_boost",  p.color_brightness_boost);
    set_float(doc, obj, "opacity_influence",       p.opacity_influence);
    set_float(doc, obj, "opacity_boost",           p.opacity_boost);
    set_float(doc, obj, "radius_influence",        p.radius_influence);
    set_float(doc, obj, "min_radius_mul",          p.min_radius_mul);
    return obj;
}

yyjson_mut_val* write_recipe(
    yyjson_mut_doc* doc, const TerrainSplatFieldRecipe& r)
{
    auto* obj = yyjson_mut_obj(doc);
    set_str(doc, obj, "height_field_name", r.height_field_name);

    yyjson_mut_obj_add_val(doc, obj, "geometry",
        write_geometry_desc(doc, r.geometry));

    set_int(doc, obj, "color_mode",
        static_cast<int>(r.color_mode));
    yyjson_mut_obj_add_val(doc, obj, "color_r",
        write_field_map(doc, r.color_r));
    yyjson_mut_obj_add_val(doc, obj, "color_g",
        write_field_map(doc, r.color_g));
    yyjson_mut_obj_add_val(doc, obj, "color_b",
        write_field_map(doc, r.color_b));
    yyjson_mut_obj_add_val(doc, obj, "color_gray",
        write_field_map(doc, r.color_gray));

    yyjson_mut_obj_add_val(doc, obj, "normal",
        write_normal_config(doc, r.normal));
    yyjson_mut_obj_add_val(doc, obj, "curvature",
        write_curvature_config(doc, r.curvature));
    yyjson_mut_obj_add_val(doc, obj, "peaks",
        write_peaks_config(doc, r.peaks));

    set_int(doc, obj, "normal_debug",
        static_cast<int>(r.normal_debug));

    return obj;
}

yyjson_mut_val* write_color_lod(
    yyjson_mut_doc* doc, const GaussianSplatColorLODCompileDesc& d)
{
    auto* obj = yyjson_mut_obj(doc);
    set_float(doc, obj, "neighbor_radius_world", d.neighbor_radius_world);
    set_float(doc, obj, "gaussian_sigma_world",  d.gaussian_sigma_world);
    set_bool(doc, obj,  "include_self",          d.include_self);
    set_bool(doc, obj,  "use_opacity_weight",    d.use_opacity_weight);
    set_float(doc, obj, "color_variance_gain",   d.color_variance_gain);
    return obj;
}

yyjson_mut_val* write_lod_settings(
    yyjson_mut_doc* doc, const wz::gpu::SplatColorLODSettings& s)
{
    auto* obj = yyjson_mut_obj(doc);
    set_int(doc, obj,   "mode",                static_cast<int>(s.mode));
    set_float(doc, obj, "strength",            s.strength);
    set_float(doc, obj, "near_distance",       s.near_distance);
    set_float(doc, obj, "far_distance",        s.far_distance);
    set_float(doc, obj, "max_stride_for_blend", s.max_stride_for_blend);
    set_bool(doc, obj,  "use_confidence",      s.use_confidence);
    return obj;
}

yyjson_mut_val* write_coverage_settings(
    yyjson_mut_doc* doc, const wz::gpu::SplatCoverageSettings& s)
{
    auto* obj = yyjson_mut_obj(doc);
    set_int(doc, obj,   "mode",              static_cast<int>(s.mode));
    set_float(doc, obj, "threshold",         s.threshold);
    set_float(doc, obj, "opacity_scale",     s.opacity_scale);
    set_int(doc, obj,   "kernel_mode",       static_cast<int>(s.kernel_mode));
    set_float(doc, obj, "radius_scale",      s.radius_scale);
    set_float(doc, obj, "inner_radius",      s.inner_radius);
    set_float(doc, obj, "outer_radius",      s.outer_radius);
    set_float(doc, obj, "gaussian_falloff",  s.gaussian_falloff);
    set_int(doc, obj,   "debug_view",        static_cast<int>(s.debug_view));
    return obj;
}

yyjson_mut_val* write_field_accum_settings(
    yyjson_mut_doc* doc,
    const wz::gpu::dx12::TerrainFieldAccumSettings& s)
{
    auto* obj = yyjson_mut_obj(doc);
    set_float(doc, obj, "weight_scale",     s.weight_scale);
    set_float(doc, obj, "ambient",          s.ambient);
    set_float(doc, obj, "diffuse_strength", s.diffuse_strength);
    set_float3(doc, obj, "light_dir",       s.light_dir);
    set_int(doc, obj,   "debug_mode",       s.debug_mode);
    return obj;
}

yyjson_mut_val* write_surface_recon(
    yyjson_mut_doc* doc, const TerrainReconDesc& d)
{
    auto* obj = yyjson_mut_obj(doc);
    set_int(doc, obj,   "grid_res",           d.grid_res);
    set_int(doc, obj,   "kernel_mode",
        static_cast<int>(d.kernel_mode));
    set_float(doc, obj, "radius_scale",       d.radius_scale);
    set_float(doc, obj, "inner_radius",       d.inner_radius);
    set_float(doc, obj, "outer_radius",       d.outer_radius);
    set_float(doc, obj, "gaussian_falloff",   d.gaussian_falloff);
    set_bool(doc, obj,  "use_gradient_normals", d.use_gradient_normals);
    return obj;
}

yyjson_mut_val* write_surface_mesh_settings(
    yyjson_mut_doc* doc,
    const wz::gpu::dx12::TerrainSurfaceMeshSettings& s)
{
    auto* obj = yyjson_mut_obj(doc);
    set_bool(doc, obj,  "wireframe",        s.wireframe);
    set_float(doc, obj, "ambient",          s.ambient);
    set_float(doc, obj, "diffuse_strength", s.diffuse_strength);
    set_float3(doc, obj, "light_dir",       s.light_dir);
    set_int(doc, obj,   "debug_mode",       s.debug_mode);
    return obj;
}

}  // anonymous namespace


// ═══════════════════════════════════════════════════════════════════════════
// save_landscape_document
// ═══════════════════════════════════════════════════════════════════════════

bool save_landscape_document(
    const LandscapeDocument& doc,
    const std::string& path,
    std::string* error)
{
    yyjson_mut_doc* jdoc = yyjson_mut_doc_new(nullptr);
    if (!jdoc)
    {
        if (error) *error = "failed to create JSON document";
        return false;
    }

    auto* root = yyjson_mut_obj(jdoc);
    yyjson_mut_doc_set_root(jdoc, root);

    // ── Header ──
    yyjson_mut_obj_add_str(jdoc, root, "schema",
        LandscapeDocument::kSchema);
    set_uint(jdoc, root, "version", doc.version);

    // ── Fields ──
    auto* fields_arr = yyjson_mut_arr(jdoc);
    for (const auto& fs : doc.fields)
        yyjson_mut_arr_add_val(fields_arr,
            write_field_source(jdoc, fs));
    yyjson_mut_obj_add_val(jdoc, root, "fields", fields_arr);

    // ── Recipe ──
    yyjson_mut_obj_add_val(jdoc, root, "terrain_recipe",
        write_recipe(jdoc, doc.terrain_recipe));
    set_bool(jdoc, root, "use_recipe_color", doc.use_recipe_color);

    // ── Color LOD ──
    set_bool(jdoc, root, "color_lod_enabled", doc.color_lod_enabled);
    yyjson_mut_obj_add_val(jdoc, root, "color_lod",
        write_color_lod(jdoc, doc.color_lod));

    // ── Preview settings ──
    auto* preview = yyjson_mut_obj(jdoc);
    set_int(jdoc, preview, "active_program", doc.active_program);

    yyjson_mut_obj_add_val(jdoc, preview, "lod",
        write_lod_settings(jdoc, doc.lod_preview));

    yyjson_mut_obj_add_val(jdoc, preview, "coverage",
        write_coverage_settings(jdoc, doc.coverage_preview));

    set_int(jdoc, preview, "force_stride", doc.force_stride);
    set_float(jdoc, preview, "target_spp", doc.target_spp);

    set_bool(jdoc, preview, "field_accum_enabled",
        doc.field_accum_enabled);
    yyjson_mut_obj_add_val(jdoc, preview, "field_accum",
        write_field_accum_settings(jdoc, doc.field_accum_preview));

    set_bool(jdoc, preview, "surface_recon_enabled",
        doc.surface_recon_enabled);
    yyjson_mut_obj_add_val(jdoc, preview, "surface_recon",
        write_surface_recon(jdoc, doc.surface_recon));
    yyjson_mut_obj_add_val(jdoc, preview, "surface_mesh",
        write_surface_mesh_settings(jdoc, doc.surface_mesh_preview));

    yyjson_mut_obj_add_val(jdoc, root, "preview", preview);

    // ── Serialize to string ──
    yyjson_write_flag flags =
        YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
    size_t len = 0;
    char* json_str = yyjson_mut_write(jdoc, flags, &len);
    yyjson_mut_doc_free(jdoc);

    if (!json_str)
    {
        if (error) *error = "yyjson serialization failed";
        return false;
    }

    // Write to disk.
    std::vector<uint8_t> bytes(json_str, json_str + len);
    free(json_str);

    auto write_err = wz::fs::write_file(path, bytes);
    if (write_err != wz::fs::FileError::None)
    {
        if (error) *error = "failed to write file: " + path;
        return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
// JSON read helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

bool jget_str(yyjson_val* obj, const char* key, std::string& out)
{
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_str(v)) return false;
    out = yyjson_get_str(v);
    return true;
}

bool jget_float(yyjson_val* obj, const char* key, float& out)
{
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_num(v)) return false;
    out = static_cast<float>(yyjson_get_num(v));
    return true;
}

bool jget_int(yyjson_val* obj, const char* key, int& out)
{
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_num(v)) return false;
    out = static_cast<int>(yyjson_get_num(v));
    return true;
}

bool jget_uint(yyjson_val* obj, const char* key, uint32_t& out)
{
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_num(v)) return false;
    out = static_cast<uint32_t>(yyjson_get_num(v));
    return true;
}

bool jget_bool(yyjson_val* obj, const char* key, bool& out)
{
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_bool(v)) return false;
    out = yyjson_get_bool(v);
    return true;
}

bool jget_float3(yyjson_val* obj, const char* key, float out[3])
{
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_arr(v) || yyjson_arr_size(v) < 3) return false;
    out[0] = static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 0)));
    out[1] = static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 1)));
    out[2] = static_cast<float>(yyjson_get_num(yyjson_arr_get(v, 2)));
    return true;
}


// ── Read sub-objects ─────────────────────────────────────────────────────

LandscapeFieldSource read_field_source(yyjson_val* obj)
{
    LandscapeFieldSource fs;
    jget_str(obj, "name",    fs.name);
    jget_str(obj, "path",    fs.path);
    jget_str(obj, "format",  fs.format);
    jget_str(obj, "channel", fs.channel);
    jget_uint(obj, "width",  fs.width);
    jget_uint(obj, "height", fs.height);
    return fs;
}

FieldMap read_field_map(yyjson_val* obj)
{
    FieldMap fm;
    if (!obj) return fm;
    jget_str(obj, "field_name", fm.field_name);
    jget_bool(obj, "normalize", fm.normalize);
    jget_float(obj, "scale",    fm.scale);
    jget_float(obj, "bias",     fm.bias);
    return fm;
}

GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc
read_geometry_desc(yyjson_val* obj)
{
    GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc d;
    if (!obj) return d;
    jget_float(obj, "height_scale",    d.height_scale);
    jget_float(obj, "step_x",          d.step_x);
    jget_float(obj, "step_z",          d.step_z);
    jget_float(obj, "overlap_factor",  d.overlap_factor);
    jget_float(obj, "thickness",       d.thickness);
    jget_uint(obj,  "subsample_step",  d.subsample_step);
    jget_float(obj, "opacity",         d.opacity);
    jget_float(obj, "flat_luminance",  d.flat_luminance);
    jget_float(obj, "steep_luminance", d.steep_luminance);
    jget_float(obj, "max_slope_stretch", d.max_slope_stretch);
    jget_bool(obj,  "normal_smoothing_enabled",
        d.normal_smoothing_enabled);
    jget_uint(obj,  "normal_smoothing_radius_cells",
        d.normal_smoothing_radius_cells);
    jget_float(obj, "normal_smoothing_sigma_cells",
        d.normal_smoothing_sigma_cells);
    return d;
}

NormalFieldConfig read_normal_config(yyjson_val* obj)
{
    NormalFieldConfig n;
    if (!obj) return n;
    int tmp = 0;
    if (jget_int(obj, "source", tmp))
        n.source = static_cast<NormalSource>(tmp);
    jget_str(obj, "field_x", n.field_x);
    jget_str(obj, "field_y", n.field_y);
    jget_str(obj, "field_z", n.field_z);
    if (jget_int(obj, "encoded_range", tmp))
        n.encoded_range = static_cast<NormalEncodedRange>(tmp);
    if (jget_int(obj, "up_axis", tmp))
        n.up_axis = static_cast<NormalUpAxis>(tmp);
    jget_bool(obj, "flip_x", n.flip_x);
    jget_bool(obj, "flip_z", n.flip_z);
    jget_float(obj, "blend", n.blend);
    return n;
}

CurvatureFieldConfig read_curvature_config(yyjson_val* obj)
{
    CurvatureFieldConfig c;
    if (!obj) return c;
    jget_str(obj,   "field_name",       c.field_name);
    jget_bool(obj,  "normalize",        c.normalize);
    jget_float(obj, "scale",            c.scale);
    jget_float(obj, "bias",             c.bias);
    jget_float(obj, "radius_influence", c.radius_influence);
    jget_float(obj, "min_radius_mul",   c.min_radius_mul);
    jget_float(obj, "opacity_influence", c.opacity_influence);
    jget_float(obj, "opacity_boost",    c.opacity_boost);
    return c;
}

PeaksFieldConfig read_peaks_config(yyjson_val* obj)
{
    PeaksFieldConfig p;
    if (!obj) return p;
    jget_str(obj,   "field_name",             p.field_name);
    jget_bool(obj,  "normalize",              p.normalize);
    jget_float(obj, "scale",                  p.scale);
    jget_float(obj, "bias",                   p.bias);
    jget_float(obj, "color_influence",         p.color_influence);
    jget_float(obj, "color_brightness_boost",  p.color_brightness_boost);
    jget_float(obj, "opacity_influence",       p.opacity_influence);
    jget_float(obj, "opacity_boost",           p.opacity_boost);
    jget_float(obj, "radius_influence",        p.radius_influence);
    jget_float(obj, "min_radius_mul",          p.min_radius_mul);
    return p;
}

TerrainSplatFieldRecipe read_recipe(yyjson_val* obj)
{
    TerrainSplatFieldRecipe r;
    if (!obj) return r;

    jget_str(obj, "height_field_name", r.height_field_name);

    r.geometry = read_geometry_desc(yyjson_obj_get(obj, "geometry"));

    int tmp = 0;
    if (jget_int(obj, "color_mode", tmp))
        r.color_mode = static_cast<TerrainSplatColorMode>(tmp);

    r.color_r    = read_field_map(yyjson_obj_get(obj, "color_r"));
    r.color_g    = read_field_map(yyjson_obj_get(obj, "color_g"));
    r.color_b    = read_field_map(yyjson_obj_get(obj, "color_b"));
    r.color_gray = read_field_map(yyjson_obj_get(obj, "color_gray"));

    r.normal    = read_normal_config(yyjson_obj_get(obj, "normal"));
    r.curvature = read_curvature_config(yyjson_obj_get(obj, "curvature"));
    r.peaks     = read_peaks_config(yyjson_obj_get(obj, "peaks"));

    if (jget_int(obj, "normal_debug", tmp))
        r.normal_debug = static_cast<NormalDebugView>(tmp);

    return r;
}

GaussianSplatColorLODCompileDesc read_color_lod(yyjson_val* obj)
{
    GaussianSplatColorLODCompileDesc d;
    if (!obj) return d;
    jget_float(obj, "neighbor_radius_world", d.neighbor_radius_world);
    jget_float(obj, "gaussian_sigma_world",  d.gaussian_sigma_world);
    jget_bool(obj,  "include_self",          d.include_self);
    jget_bool(obj,  "use_opacity_weight",    d.use_opacity_weight);
    jget_float(obj, "color_variance_gain",   d.color_variance_gain);
    return d;
}

wz::gpu::SplatColorLODSettings read_lod_settings(yyjson_val* obj)
{
    wz::gpu::SplatColorLODSettings s;
    if (!obj) return s;
    int tmp = 0;
    if (jget_int(obj, "mode", tmp))
        s.mode = static_cast<wz::gpu::SplatColorLODMode>(tmp);
    jget_float(obj, "strength",            s.strength);
    jget_float(obj, "near_distance",       s.near_distance);
    jget_float(obj, "far_distance",        s.far_distance);
    jget_float(obj, "max_stride_for_blend", s.max_stride_for_blend);
    jget_bool(obj,  "use_confidence",      s.use_confidence);
    return s;
}

wz::gpu::SplatCoverageSettings read_coverage_settings(yyjson_val* obj)
{
    wz::gpu::SplatCoverageSettings s;
    if (!obj) return s;
    int tmp = 0;
    if (jget_int(obj, "mode", tmp))
        s.mode = static_cast<wz::gpu::SplatCoverageMode>(tmp);
    jget_float(obj, "threshold",        s.threshold);
    jget_float(obj, "opacity_scale",    s.opacity_scale);
    if (jget_int(obj, "kernel_mode", tmp))
        s.kernel_mode = static_cast<TerrainCoverageKernelMode>(tmp);
    jget_float(obj, "radius_scale",     s.radius_scale);
    jget_float(obj, "inner_radius",     s.inner_radius);
    jget_float(obj, "outer_radius",     s.outer_radius);
    jget_float(obj, "gaussian_falloff", s.gaussian_falloff);
    if (jget_int(obj, "debug_view", tmp))
        s.debug_view = static_cast<wz::gpu::TerrainCoverageDebugView>(tmp);
    return s;
}

wz::gpu::dx12::TerrainFieldAccumSettings
read_field_accum_settings(yyjson_val* obj)
{
    wz::gpu::dx12::TerrainFieldAccumSettings s;
    if (!obj) return s;
    jget_float(obj, "weight_scale",     s.weight_scale);
    jget_float(obj, "ambient",          s.ambient);
    jget_float(obj, "diffuse_strength", s.diffuse_strength);
    jget_float3(obj, "light_dir",       s.light_dir);
    jget_int(obj, "debug_mode",         s.debug_mode);
    return s;
}

TerrainReconDesc read_surface_recon(yyjson_val* obj)
{
    TerrainReconDesc d;
    if (!obj) return d;
    jget_int(obj,   "grid_res",           d.grid_res);
    int tmp = 0;
    if (jget_int(obj, "kernel_mode", tmp))
        d.kernel_mode = static_cast<TerrainCoverageKernelMode>(tmp);
    jget_float(obj, "radius_scale",       d.radius_scale);
    jget_float(obj, "inner_radius",       d.inner_radius);
    jget_float(obj, "outer_radius",       d.outer_radius);
    jget_float(obj, "gaussian_falloff",   d.gaussian_falloff);
    jget_bool(obj,  "use_gradient_normals", d.use_gradient_normals);
    return d;
}

wz::gpu::dx12::TerrainSurfaceMeshSettings
read_surface_mesh_settings(yyjson_val* obj)
{
    wz::gpu::dx12::TerrainSurfaceMeshSettings s;
    if (!obj) return s;
    jget_bool(obj,  "wireframe",        s.wireframe);
    jget_float(obj, "ambient",          s.ambient);
    jget_float(obj, "diffuse_strength", s.diffuse_strength);
    jget_float3(obj, "light_dir",       s.light_dir);
    jget_int(obj,   "debug_mode",       s.debug_mode);
    return s;
}

}  // anonymous namespace


// ═══════════════════════════════════════════════════════════════════════════
// load_landscape_document
// ═══════════════════════════════════════════════════════════════════════════

LandscapeLoadResult load_landscape_document(const std::string& path)
{
    LandscapeLoadResult result;

    auto file_data = wz::fs::read_file(path);
    if (file_data.error != wz::fs::FileError::None)
    {
        result.error = "failed to read file: " + path;
        return result;
    }

    yyjson_doc* jdoc = yyjson_read(
        reinterpret_cast<const char*>(file_data.value.data()),
        file_data.value.size(), 0);
    if (!jdoc)
    {
        result.error = "failed to parse JSON";
        return result;
    }

    yyjson_val* root = yyjson_doc_get_root(jdoc);
    if (!root || !yyjson_is_obj(root))
    {
        result.error = "root is not a JSON object";
        yyjson_doc_free(jdoc);
        return result;
    }

    // ── Validate schema ──
    std::string schema;
    jget_str(root, "schema", schema);
    if (schema != LandscapeDocument::kSchema)
    {
        result.error = "unexpected schema: " + schema;
        yyjson_doc_free(jdoc);
        return result;
    }

    auto& doc = result.document;
    jget_uint(root, "version", doc.version);

    // ── Fields ──
    yyjson_val* fields_arr = yyjson_obj_get(root, "fields");
    if (fields_arr && yyjson_is_arr(fields_arr))
    {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(fields_arr, idx, max, val)
        {
            doc.fields.push_back(read_field_source(val));
        }
    }

    // ── Recipe ──
    doc.terrain_recipe = read_recipe(
        yyjson_obj_get(root, "terrain_recipe"));
    jget_bool(root, "use_recipe_color", doc.use_recipe_color);

    // ── Color LOD ──
    jget_bool(root, "color_lod_enabled", doc.color_lod_enabled);
    doc.color_lod = read_color_lod(
        yyjson_obj_get(root, "color_lod"));

    // ── Preview settings ──
    yyjson_val* preview = yyjson_obj_get(root, "preview");
    if (preview && yyjson_is_obj(preview))
    {
        jget_int(preview, "active_program", doc.active_program);

        doc.lod_preview = read_lod_settings(
            yyjson_obj_get(preview, "lod"));

        doc.coverage_preview = read_coverage_settings(
            yyjson_obj_get(preview, "coverage"));

        jget_int(preview, "force_stride", doc.force_stride);
        jget_float(preview, "target_spp", doc.target_spp);

        jget_bool(preview, "field_accum_enabled",
            doc.field_accum_enabled);
        doc.field_accum_preview = read_field_accum_settings(
            yyjson_obj_get(preview, "field_accum"));

        jget_bool(preview, "surface_recon_enabled",
            doc.surface_recon_enabled);
        doc.surface_recon = read_surface_recon(
            yyjson_obj_get(preview, "surface_recon"));
        doc.surface_mesh_preview = read_surface_mesh_settings(
            yyjson_obj_get(preview, "surface_mesh"));
    }

    yyjson_doc_free(jdoc);
    result.ok = true;
    return result;
}

}  // namespace wz::engine::assets
