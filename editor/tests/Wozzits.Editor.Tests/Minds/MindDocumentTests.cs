using System;
using System.IO;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Minds;

namespace Wozzits.Editor.Tests.Minds;

/// <summary>The mind document's save path: it writes the edited mind back to its .mind.json
/// and the hand-placed layout to a .mind.editor.json sidecar, and restores the layout on
/// open. Uses a temp directory (real file I/O, like the statechart document does).</summary>
public sealed class MindDocumentTests
{
    private static string TempDir()
    {
        var dir = Path.Combine(Path.GetTempPath(), "wz_mind_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return dir;
    }

    [Fact]
    public void Save_Writes_The_Mind_And_Layout_Sidecar()
    {
        var dir = TempDir();
        try
        {
            var path = Path.Combine(dir, "m.mind.json");
            var mind = new Mind { Name = "m" };
            mind.Qubits.Add(new MindQubit { Id = "q0", Goal = 0.5 });
            mind.Qubits.Add(new MindQubit { Id = "q1" });
            mind.Bonds.Add(new MindBond { A = "q0", B = "q1", J = -0.3 });
            File.WriteAllText(path, MindJson.Emit(mind, indented: true));

            var doc = new MindDocumentViewModel("m", path, MindJson.Load(File.ReadAllText(path)));
            doc.Pane.SelectOnly(doc.Pane.Nodes[1]);   // q1
            doc.Pane.DeleteSelected();                // removes q1 + the q0<->q1 bond
            Assert.True(doc.IsDirty);

            doc.Save();

            Assert.True(File.Exists(path));
            Assert.True(File.Exists(Path.Combine(dir, "m.mind.editor.json")));
            Assert.False(doc.IsDirty);   // cleared after save

            var reloaded = MindJson.Load(File.ReadAllText(path));
            Assert.Single(reloaded.Qubits);   // q1 was deleted
            Assert.Empty(reloaded.Bonds);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    [Fact]
    public void Reopen_Restores_Hand_Placed_Positions_From_The_Sidecar()
    {
        var dir = TempDir();
        try
        {
            var path = Path.Combine(dir, "m.mind.json");
            var mind = new Mind { Name = "m" };
            mind.Qubits.Add(new MindQubit { Id = "q0" });
            mind.Qubits.Add(new MindQubit { Id = "q1" });
            File.WriteAllText(path, MindJson.Emit(mind, indented: true));

            // Open, hand-place q0, save the sidecar.
            var doc = new MindDocumentViewModel("m", path, MindJson.Load(File.ReadAllText(path)));
            doc.Pane.SelectOnly(doc.Pane.Nodes[0]);
            doc.Pane.MoveSelectedBy(137, 42);
            double x = doc.Pane.Nodes[0].X;
            double y = doc.Pane.Nodes[0].Y;
            doc.Save();

            // Reopen -> the sidecar restores the position over the auto-layout.
            var reopened = new MindDocumentViewModel("m", path, MindJson.Load(File.ReadAllText(path)));
            Assert.Equal(x, reopened.Pane.Nodes[0].X);
            Assert.Equal(y, reopened.Pane.Nodes[0].Y);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }
}
