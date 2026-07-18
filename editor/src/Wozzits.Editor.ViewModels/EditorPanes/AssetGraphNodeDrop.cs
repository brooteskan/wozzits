namespace Wozzits.Editor.ViewModels.EditorPanes;

// One node in a cross-pane drag: which node moved, and where it sat relative to the node
// actually under the cursor, so a multi-node drag keeps its shape when it lands
// (issue woguls/wozzits-editor#1).
public readonly record struct AssetGraphNodeDrop(
    ulong NodeId,
    double OffsetX,
    double OffsetY);
