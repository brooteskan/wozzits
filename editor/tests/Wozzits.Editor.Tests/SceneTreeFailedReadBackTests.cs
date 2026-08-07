using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P039. A scene read-back that produced NO ANSWER used to be indistinguishable
/// from an empty scene, and the editor acted on it: LoadSnapshot cleared the tree
/// BEFORE it branched, and MergeGraftedNodes dropped the merged grafts BEFORE it
/// fetched. Both destroyed live state on the way to reporting a failure, while the
/// scene was intact in the engine and on disk.
/// </summary>
public sealed class SceneTreeFailedReadBackTests
{
    private static EngineSceneSnapshotResponse SceneWith(params string[] rootIds)
    {
        return new EngineSceneSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineSceneSnapshot
            {
                Roots = [.. rootIds.Select(id => new EngineSceneNode { Id = id })],
            },
        };
    }

    // The control: a SUCCESSFUL empty snapshot must still clear the tree. Without
    // this, "the tree survived" would also pass for a LoadSnapshot that had stopped
    // updating anything at all.
    [Fact]
    public void AnEmptySuccessfulSnapshotStillClearsTheTree()
    {
        var pane = new SceneTreeEditorPaneViewModel();
        pane.LoadSnapshot(SceneWith("root"));
        Assert.Single(pane.Nodes);

        pane.LoadSnapshot(SceneWith());

        Assert.Empty(pane.Nodes);
        Assert.Equal("Scene has no nodes.", pane.EmptyState);
    }

    [Fact]
    public void AFailedSnapshotLeavesTheExistingTreeAlone()
    {
        var pane = new SceneTreeEditorPaneViewModel();
        pane.LoadSnapshot(SceneWith("root"));

        pane.LoadSnapshot(new EngineSceneSnapshotResponse
        {
            Ok = false,
            Error = "the engine did not answer",
        });

        Assert.Single(pane.Nodes);
        Assert.Equal("Could not load scene: the engine did not answer", pane.EmptyState);
    }
}
