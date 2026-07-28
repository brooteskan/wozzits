#include <engine/assets/scene/scenelet_authoring.h>

#include <string>

namespace wz::engine::assets
{
    SceneAssetData make_minimal_scenelet(std::string_view name)
    {
        // AuthoredTransform already defaults to identity (0,0,0 / 0,0,0,1 /
        // 1,1,1) and SceneNodeAsset to visible+active, so the minimal document
        // is just "one named root with no parent". Anything more would be this
        // function inventing policy the editor is better placed to author.
        SceneNodeAsset root;
        root.id = std::string(kSceneletRootNodeId);
        root.parent_id = std::nullopt;
        root.name = std::string(name);

        SceneAssetData scene;
        scene.name = std::string(name);
        scene.nodes.push_back(std::move(root));
        return scene;
    }

    bool is_valid_scenelet_name(std::string_view name)
    {
        if (name.empty()) {
            return false;
        }

        // Blank-but-not-empty ("   ") would produce a file nobody can select in
        // the menu, so treat it as invalid rather than minting it.
        bool all_blank = true;
        for (const char ch : name) {
            if (ch != ' ' && ch != '\t') {
                all_blank = false;
                break;
            }
        }
        if (all_blank) {
            return false;
        }

        // A scenelet name is a FILE STEM, never a path: reject anything that
        // could redirect the write out of the scenelets folder.
        if (name.find('/') != std::string_view::npos
            || name.find('\\') != std::string_view::npos
            || name.find(':') != std::string_view::npos
            || name.find("..") != std::string_view::npos)
        {
            return false;
        }

        // Control characters and the Windows-reserved set; the editor used to
        // apply Path.GetInvalidFileNameChars() itself, which is exactly the
        // knowledge that belongs on this side of the ABI.
        for (const char ch : name) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (uch < 0x20u) {
                return false;
            }
            if (ch == '<' || ch == '>' || ch == '"' || ch == '|'
                || ch == '?' || ch == '*')
            {
                return false;
            }
        }
        return true;
    }

    wz::fs::Path scenelets_folder_for_scene(const wz::fs::Path& scene_path)
    {
        // Mirrors register_scenelet_prefabs: the folder sits next to the scene
        // file, expressed relative to the resource root (an empty scene dir —
        // a scene at the resource root — yields a bare "scenelets").
        const wz::fs::Path scene_dir = wz::fs::parent_path(scene_path);
        return scene_dir.empty()
            ? wz::fs::Path{ "scenelets" }
            : wz::fs::join(scene_dir, "scenelets");
    }

    wz::fs::Path scenelet_relative_path(
        const wz::fs::Path& scene_path,
        std::string_view name)
    {
        return wz::fs::join(
            scenelets_folder_for_scene(scene_path),
            std::string(name) + ".scene.json");
    }

} // namespace wz::engine::assets
