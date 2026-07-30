using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

// One entry in the project's scenelet (prefab) catalog: the prefab NAME (filename
// stem, e.g. "enemy_tank") + the resource-relative scene-file PATH used to open it.
public sealed record SceneletInfo(string Name, string Path);

public interface IWozzitsEngineEditorSession
{
    // True while the engine's viewport runtime is alive. Scene-tree structural
    // edits (add child / reparent / delete / export subtree) run against that
    // live runtime, so callers gate on this: false means the viewport was never
    // started or its window was closed, and those edits are unavailable.
    bool IsRuntimeRunning { get; }

    EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot();

    // Device-free authoring catalog of asset-graph node types (project- and
    // session-independent), excluding types not yet migrated to wozzits-rhi.
    EngineAssetCatalogResponse LoadAssetCatalog();

    // Device-free catalog of behavior-registered actuators (name/label + declared
    // arg schema) a statechart `call` effect can invoke. Project- and session-
    // independent, like LoadAssetCatalog (built from a throwaway registry).
    EngineActuatorCatalogResponse LoadActuatorCatalog();

    // Device-free catalog of behavior MODULES and the config params each DECLARES
    // (key/type/default), so the inspector can render typed, discoverable config fields
    // for a behavior. Project- and session-independent, like LoadActuatorCatalog.
    EngineBehaviorModuleCatalogResponse LoadBehaviorModuleCatalog();

    // Device-free, READ-ONLY import of a GLB scene's component hierarchy (issue
    // #213 Phase 3b-1), so the inspector can show what a node's glb_scene_source
    // descriptor grafts as children. glbPath is the authored source_path verbatim
    // (relative or absolute); the engine roots it against the project's resource
    // root. Returns Ok=false (with Error) when the GLB cannot be read/imported.
    EngineGlbSceneHierarchy ImportGlbSceneHierarchy(
        string glbPath,
        uint sceneIndex);

    EngineAssetGraphConnectionCheckResponse CanConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort);

    EngineAssetGraphConnectionCheckResponse ConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort);

    EngineMutationResponse DisconnectAssetGraphEdge(ulong edgeId);

    // Add a new authored node for (schema, type) to the draft; returns its id.
    EngineAddNodeResponse AddAssetGraphNode(ulong schema, uint type);

    // Find-or-create the shared "Inochi shared assets" subgraph (the puppet
    // render program + its two staged shaders) in the draft. Idempotent; returns
    // the puppet-program node id.
    EngineAddNodeResponse AddInochiSharedAssets();

    // Remove a node (and edges touching it) from the draft.
    EngineMutationResponse RemoveAssetGraphNode(ulong nodeId);

    EngineMutationResponse SetAssetGraphNodeParamString(
        ulong nodeId,
        string name,
        string value);

    EngineMutationResponse SaveAssetGraph();

    EngineMutationResponse CommitAssetGraph();

    EngineMutationResponse CompileAssetGraph();

    EngineMutationResponse SetAssetGraphNodePosition(
        ulong nodeId,
        double x,
        double y);

    EngineMutationResponse SetAssetGraphZoom(double zoom);

    // DEAD file-based twins -- do not route an edit through these. Their engine
    // exports (wz_host_scene_set_node_properties / _node_transform / _camera) cast
    // every value argument to (void) and return
    // "scene authoring mutations are not implemented in the engine ABI yet", so the
    // call always FAILS and nothing is ever written. Every working edit goes through
    // the runtime seam instead: the *Live members below, which post to the running
    // engine and are persisted by SaveScene.
    //
    // They are kept because the naming pair (SetSceneNodeX vs SetSceneNodeXLive) is
    // the tell that distinguishes the two families, and deleting half of it would
    // make the surviving half look arbitrary. Marked rather than deleted so the next
    // reader does not have to trace the export to find out which one persists.
    EngineMutationResponse SetSceneNodeProperties(
        string nodeId,
        string name,
        bool visible);

    EngineMutationResponse SetSceneNodeTransform(
        string nodeId,
        EngineSceneTransformEdit edit);

    // Live transform preview: push a node transform to the running viewport
    // engine (no disk write). No-op success when no viewport is running.
    EngineMutationResponse SetSceneNodeTransformLive(
        string nodeId,
        EngineSceneTransformEdit edit);

    // Live node label/visibility edit to the running scene; no-op success when
    // no viewport is running.
    EngineMutationResponse SetSceneNodePropertiesLive(
        string nodeId,
        string name,
        bool visible);

    // Reparent a node under newParentId (empty => top level) in the running
    // scene; no-op when no viewport is running. The engine re-validates.
    EngineMutationResponse ReparentNode(string nodeId, string newParentId);

    // Move nodeId to just before beforeNodeId in the flat DRAW order (empty
    // before-id => move to the end / drawn last). Draw order only — nesting is
    // parent_id-based and untouched; no-op when no viewport is running. The
    // engine re-applies the render_order layer sort, so a reorder is within a
    // layer.
    EngineMutationResponse ReorderNode(string nodeId, string beforeNodeId);

    // Set a node's render_order (draw-order LAYER: -200 Sky / 0 World / 100
    // Transparent / 200 Overlay); the engine re-bakes draw order so the node
    // moves to its layer. No-op when no viewport is running. This is the cross-
    // cutting layer override (within a layer, order is the tree / reorder).
    EngineMutationResponse SetNodeRenderOrder(string nodeId, int renderOrder);

    // Remove a node and its subtree from the running scene; no-op when no
    // viewport is running.
    EngineMutationResponse RemoveNode(string nodeId);

    // Persist the running scene to its source file. FAILS (Ok = false) when no
    // viewport is running: the scene lives in the engine, so there is nothing to
    // persist and callers must not report it as saved. Also happens
    // automatically when the runtime exits.
    EngineMutationResponse SaveScene();

    // Carve the scene node `rootNodeId` (its authored string id) + its descendants
    // out of the running scene and write them as a standalone scene.json (a
    // reusable prefab / "scenelet") at the absolute `outPath`. Requires a running
    // viewport; errors when none is running (there is nothing to export).
    EngineMutationResponse ExportSubtreeAsScene(string rootNodeId, string outPath);

    // Reload the project's (freshly recompiled) behavior-module DLLs into the
    // running engine without restarting it: the engine clears its behavior
    // registry, re-registers built-ins, reloads the project DLLs, and rebuilds
    // the behavior scene on its next frame. Errors when no viewport is running
    // (there is nothing to reload into). Compiling the DLLs is a separate
    // editor-side step (BehaviorModuleBuilder); this only reloads them.
    EngineMutationResponse ReloadBehaviorModules();

    // Names of the behavior modules currently registered in the running engine
    // (built-ins + loaded project DLLs) — the set a node behavior binding may
    // reference. Empty when no viewport is running. Used to offer "add a
    // behavior" from the imported modules.
    IReadOnlyList<string> GetBehaviorModuleCatalog();

    // Edits the engine could not apply because the target node was gone. The
    // mutation verbs return OK before the id is resolved, so this is how a dropped
    // edit becomes visible at all. TAKE semantics: each is reported once.
    IReadOnlyList<string> TakeDroppedEdits();

    // The project's scenelets (prefabs) for the scenelet menu: each is a name +
    // resource-relative scene-file path. Empty when no viewport is running. The
    // path feeds OpenScene to edit that prefab in place.
    IReadOnlyList<SceneletInfo> GetSceneletCatalog();

    // Swap the running viewport's WORKING SCENE to `scenePath` -- open a scenelet
    // (from GetSceneletCatalog) to edit it with the normal tools, or pass an EMPTY
    // string to switch back to the project's main scene. Reuses the project asset
    // graph. Blocks until the engine loads it; the caller should reload the scene
    // via LoadRuntimeSceneSnapshot afterward. Errors when no viewport is running.
    EngineMutationResponse OpenScene(string scenePath);

    // Create a new, minimal scenelet named `name` and return its resource-
    // relative path -- the form OpenScene accepts. Device-free (no running
    // viewport needed): the engine owns where scenelets live and what a fresh
    // one contains, so the editor supplies only the name (issue #271).
    EngineCreateSceneletResponse CreateScenelet(string name);

    // Set one config entry on every behaviour binding in the scene FILE at
    // `sceneRelativePath` matching `module` and `matchKey` == `matchValue`.
    // The file-target twin of SetNodeBehaviorConfig, which only reaches the
    // RUNNING scene: this is how a freshly compiled chart/mind IR reaches
    // scenelets that are not the open document (issue #303). Idempotent --
    // UpdatedCount 0 means the file already matched and was not rewritten.
    EngineSceneFileConfigResponse SetSceneFileBehaviorConfig(
        string sceneRelativePath,
        string module,
        string matchKey,
        string matchValue,
        string configKey,
        string value);

    // Add a child node under parentId (empty => top level) in the running scene
    // and return the engine-minted id. Errors if no viewport is running.
    EngineAddSceneNodeResponse AddChildNode(string parentId);

    // The running scene's runtime-only GRAFTED nodes (issue #213): the instance-
    // grafted sub-trees a "Subtree from asset" reference produces, which live only
    // in the live runtime and are absent from the saved scene.json. Each returned
    // root carries its host node's id as ParentId so the caller can merge the
    // sub-tree under that host in the JSON-sourced tree. An empty snapshot (no
    // roots) when no viewport is running or nothing is grafted.
    EngineSceneSnapshot LoadGraftedSceneNodes();

    // The running scene's authored nodes as a full scene snapshot -- used to
    // rebuild the tree after OpenScene swaps the working scene (open a scenelet, or
    // switch back). Empty response when no viewport is running.
    EngineSceneSnapshotResponse LoadRuntimeSceneSnapshot();

    // DEAD file-based twin, like SetSceneNodeProperties/SetSceneNodeTransform above:
    // wz_host_scene_set_camera discards its arguments and fails. Camera edits go
    // through SetRuntimeSceneNodeCamera.
    EngineMutationResponse SetSceneNodeCamera(
        string nodeId,
        EngineSceneCameraEdit edit);

    // Live behavior-binding authoring on the running scene (deferred, host-gated
    // engine-side; no-op success when no viewport is running). Add returns the
    // engine-minted binding id.
    EngineAddSceneNodeResponse AddNodeBehavior(string nodeId, string module);

    EngineMutationResponse RemoveNodeBehavior(string nodeId, string bindingId);

    EngineMutationResponse SetNodeBehaviorEnabled(
        string nodeId,
        string bindingId,
        bool enabled);

    EngineMutationResponse SetNodeBehaviorFields(
        string nodeId,
        string bindingId,
        string label,
        string module);

    EngineMutationResponse SetNodeBehaviorEvents(
        string nodeId,
        string bindingId,
        string events);

    EngineMutationResponse SetNodeBehaviorConfig(
        string nodeId,
        string bindingId,
        string key,
        string kind,
        string value);

    EngineMutationResponse ClearNodeBehaviorConfig(
        string nodeId,
        string bindingId,
        string key);

    // Live add/remove of an optional node component by kind ("camera",
    // "renderable", "proximity", "collision", "motion") on the running scene;
    // deferred + host-gated engine-side, no-op success when no viewport is
    // running.
    EngineMutationResponse AddNodeComponent(string nodeId, string kind);

    EngineMutationResponse RemoveNodeComponent(string nodeId, string kind);

    // Set (0 clears) the node's preferred asset-graph renderable by the
    // asset-graph node id it points at; live + host-gated, no-op success when no
    // viewport is running. Never touches the legacy embedded renderable slot.
    EngineMutationResponse SetNodeRenderableAsset(
        string nodeId,
        ulong assetGraphNodeId);

    // Set (0 clears) the node's AudioSource renderable reference by the
    // audio-renderable asset-graph node id it points at (creating the component
    // on a non-zero pick); live + host-gated, no-op success when no viewport is
    // running.
    EngineMutationResponse SetNodeAudioRenderable(
        string nodeId,
        ulong assetGraphNodeId);

    // Set an AudioSource's per-entity play policy (auto_play / enabled). Live +
    // host-gated; no-op if the node has no AudioSource component.
    EngineMutationResponse SetNodeAudioSourcePlay(
        string nodeId,
        bool autoPlay,
        bool enabled);

    // Author (0 clears) the node's GEOMETRY ingredient — a Mesh / GpuSparseMesh
    // asset-graph node by its node id. The engine assembles the RHI renderable
    // from this geometry plus the node's effective render program (#213 increment
    // 2). Live + host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeGeometryAsset(
        string nodeId,
        ulong assetGraphNodeId);

    // Author (0 clears) the node's RENDER-PROGRAM ingredient — a render-program
    // asset-graph node by its node id. The program is INHERITED down the scene
    // tree, so it cascades to descendants without their own program (#213
    // increment 2). Live + host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeRenderProgram(
        string nodeId,
        ulong assetGraphNodeId);

    // Author ONE semantic resource binding of the node's custom-renderable
    // ingredients (issue #229/#230): upsert the row for the layout semantic
    // pointing at an asset-graph source node (0 removes the row). A binding
    // present makes the assembled renderable the CUSTOM (0x70A) form. Live +
    // host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeRenderableBinding(
        string nodeId,
        string semantic,
        ulong assetGraphNodeId);

    // Author ONE per-instance constant override of the node's custom-renderable
    // ingredients (issue #229/#230): upsert the named layout constant with 4
    // floats (null removes the override). Overrides merge at PACK time — no
    // asset recompile; the next rendered frame reflects the edit. Live +
    // host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeRenderableParam(
        string nodeId,
        string name,
        float[]? valueXyzw);

    // Author the node's COLLISION component (terrain-stick track): point it at a
    // Collision asset-graph node (0 clears the reference) + a constrain-movement
    // flag (whether the node's movement is constrained by that collision data).
    // Live + host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeCollision(
        string nodeId,
        ulong assetGraphNodeId,
        bool constrainMovement);

    // Author a node's Atmosphere component: which Atmosphere asset-graph node the
    // frame's fog reads (0 clears the reference) + an enabled flag. Live +
    // host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeAtmosphere(
        string nodeId,
        ulong atmosphereAssetNodeId,
        bool enabled);

    // Author a node's FrameEnvironment component: which FrameEnvironment asset-graph
    // node the frame's environment reads (0 clears the reference) + an enabled flag.
    // Live + host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeEnvironment(
        string nodeId,
        ulong environmentAssetNodeId,
        bool enabled);

    // Author the node's MOTION component terrain-constraint fields (terrain-stick
    // track): whether the node sticks to the terrain surface, ride height,
    // footprint radius, surface-alignment toggle, and alignment strength. Live +
    // host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeMotionTerrain(
        string nodeId,
        bool terrainConstrained,
        float rideHeight,
        float footprintRadius,
        bool alignToSurface,
        float alignmentStrength);

    // Author the node's whole MOTION FILTER component (per-DOF smoothing/clamp
    // for secondary-motion camera damping). Live + host-gated, no-op success
    // when no viewport is running.
    EngineMutationResponse SetNodeMotionFilter(
        string nodeId,
        EngineSceneNodeMotionFilter filter);

    // Point (0 clears) the node at a "Scene from GLB" asset-graph node by its node
    // id; the runtime grafts that GLB's hierarchy as the node's children (issue
    // #213 piece 2). consumeMode: 0 instance / 1 flatten. Live + host-gated, no-op
    // success when no viewport is running.
    EngineMutationResponse SetNodeSceneSource(
        string nodeId,
        ulong assetGraphNodeId,
        uint consumeMode);

    // Author (empty glbPath clears) the node's GLB scene-source descriptor — the
    // asset-graph-independent route (issue #213 Phase 3a): a resource-relative GLB
    // path + scene index + consume mode (0 instance / 1 flatten). The engine
    // re-resolves it into a Scene and grafts the GLB hierarchy under the node.
    // Live + host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeGlbSceneSource(
        string nodeId,
        string glbPath,
        uint sceneIndex,
        uint consumeMode);

    // Assign a per-component render style into the node's GLB scene-source
    // descriptor (issue #213 Phase 3b-2). targetBase: true = the descriptor's base
    // style (applies to all imported meshes); false = a per-mesh override for
    // meshIndex. The style subset is surface/wireframe enabled + RGBA (a null rgba
    // = engine-default color). The engine writes it into the persisted descriptor
    // and re-materializes the re-keyed Scene. Live + host-gated, no-op success when
    // no viewport is running.
    EngineMutationResponse SetNodeGlbComponentStyle(
        string nodeId,
        bool targetBase,
        uint meshIndex,
        bool surfaceEnabled,
        float[]? surfaceRgba,
        bool wireframeEnabled,
        float[]? wireframeRgba);

    // Clear the per-mesh-index render-style override for meshIndex in the node's
    // GLB scene-source descriptor (the mesh falls back to the base style). Live +
    // host-gated, no-op success when no viewport is running (issue #213 3b-2).
    EngineMutationResponse ClearNodeGlbComponentStyle(string nodeId, uint meshIndex);

    // Enable/disable frame profiling on the running viewport engine (default
    // off). No-op when no viewport is running. While enabled the engine records a
    // per-frame profile and writes frame_profile_<tag>.csv on the on->off toggle
    // and on app close; off records nothing and writes no file.
    void SetFrameProfiling(bool enabled);

    // Stop the engine's viewport runtime (if any) and start a fresh one for the
    // current project - used to reopen the viewport after its window was closed.
    void RestartRuntime();

    // Pause/resume the running viewport's simulation (default running). While paused
    // the viewport keeps rendering the last frame but stops advancing behaviors /
    // agents / time, freeing that CPU. No-op when no viewport is running.
    void SetSimulationPaused(bool paused);
}
