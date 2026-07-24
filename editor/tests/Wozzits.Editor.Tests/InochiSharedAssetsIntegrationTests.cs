using System;
using System.IO;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.ViewModels.EditorPanes;
using Xunit;
using Xunit.Abstractions;

namespace Wozzits.Editor.Tests;

// Integration coverage for the "Add Inochi shared assets" editor action, driving
// the REAL native session (wozzits_abi.dll) against a throwaway project. Verifies
// the action authors the shared puppet-program subgraph AND wraps it in a visible
// "Inochi Shared Assets" sub-graph, both for a fresh graph and idempotently.
public sealed class InochiSharedAssetsIntegrationTests
{
    private readonly ITestOutputHelper _out;

    public InochiSharedAssetsIntegrationTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void AddInochiSharedAssets_FreshProject_CreatesVisibleGroupOfFive()
    {
        var projectDir = Path.Combine(
            Path.GetTempPath(), "wz_inochi_test_" + Guid.NewGuid().ToString("n"));
        Directory.CreateDirectory(Path.Combine(projectDir, ".wozzits"));
        File.WriteAllText(
            Path.Combine(projectDir, ".wozzits", "project.json"),
            """{"schema":"wozzits.project.v1","formatVersion":1,"name":"InochiTest","asset_graph":"assets.graph.json"}""");
        File.WriteAllText(
            Path.Combine(projectDir, "assets.graph.json"),
            """{"schema":"wozzits.asset_graph.v2","nodes":[]}""");

        try
        {
            var engine = new WozzitsEngineNativeClient();
            var session = engine.OpenEditorSession(projectDir);
            try
            {
                var grouping = new AssetGraphGroupingModel();
                var pane = new AssetGraphEditorPaneViewModel(session, grouping);
                pane.LoadSnapshot(session.LoadAssetGraphSnapshot());

                var ok = pane.AddInochiSharedAssets();
                _out.WriteLine(
                    $"call1 ok={ok} err='{pane.LastEditError}' groups={grouping.SubGraphs.Count}");
                foreach (var sg in grouping.SubGraphs)
                {
                    _out.WriteLine($"  '{sg.Name}' members={sg.MemberCount}");
                }

                Assert.True(ok, pane.LastEditError);
                Assert.Single(grouping.SubGraphs);
                Assert.Equal("Inochi Shared Assets", grouping.SubGraphs[0].Name);
                Assert.Equal(5, grouping.SubGraphs[0].MemberCount);

                // Idempotent re-click: the engine adds nothing and the group is not
                // duplicated (the returned program node is already grouped).
                var ok2 = pane.AddInochiSharedAssets();
                Assert.True(ok2, pane.LastEditError);
                Assert.Single(grouping.SubGraphs);
                Assert.Equal(5, grouping.SubGraphs[0].MemberCount);

                // The shaders were staged into the project so the subgraph resolves.
                Assert.True(File.Exists(
                    Path.Combine(projectDir, "shaders", "puppet", "puppet_vs.hlsl")));
                Assert.True(File.Exists(
                    Path.Combine(projectDir, "shaders", "puppet", "puppet_ps.hlsl")));
            }
            finally
            {
                (session as IDisposable)?.Dispose();
            }
        }
        finally
        {
            try { Directory.Delete(projectDir, recursive: true); }
            catch { /* best effort */ }
        }
    }
}
