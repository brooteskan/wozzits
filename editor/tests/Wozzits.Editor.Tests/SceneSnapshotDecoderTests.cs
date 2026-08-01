using System.Reflection;
using System.Runtime.InteropServices;
using Wozzits.Editor.HostClient;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-T2: the Readers suite driven by SYNTHETIC buffers. Every other test in this
/// repo that touches HostClient short-circuits on empty input, so before this file
/// no test reached a decoder with real bytes at all.
///
/// The buffers are laid out from Marshal.OffsetOf/SizeOf rather than hardcoded
/// offsets, so an ABI bump moves them automatically instead of silently testing
/// the wrong bytes.
/// </summary>
public sealed class SceneSnapshotDecoderTests
{
    private static readonly Assembly HostClient =
        typeof(WozzitsEngineNativeClient).Assembly;

    private static Type T(string name) =>
        HostClient.GetType("Wozzits.Editor.HostClient." + name)
        ?? throw new InvalidOperationException("missing type " + name);

    private static int Off(Type t, string field) => (int)Marshal.OffsetOf(t, field);

    // Header + `levels` tables of `branch` node records each. Level i's nodes all
    // point their Children span at level i+1's table -- so every node at a level
    // SHARES one child table. That is a DAG, not a cycle: max depth is levels-1,
    // well under MaxSceneNodeDepth, while the decoded node count is 2^levels-1.
    private static byte[] BuildSharedChildTableBlob(int levels, int branch)
    {
        var tSnap = T("WzEditorProjectSnapshotAbi");
        var tScene = T("WzEditorSceneSnapshotAbi");
        var tNode = T("WzEditorSceneNodeAbi");
        var tTable = T("WzEditorTableSpanAbi");

        var snapSize = Marshal.SizeOf(tSnap);
        var nodeSize = Marshal.SizeOf(tNode);
        var tableBase = (snapSize + 7) / 8 * 8;
        var blob = new byte[tableBase + levels * branch * nodeSize];

        var abiVersion = (uint)T("WozzitsEngineAbi")
            .GetField("AbiVersion", BindingFlags.NonPublic | BindingFlags.Static)!
            .GetRawConstantValue()!;
        BitConverter.TryWriteBytes(
            blob.AsSpan(Off(tSnap, "AbiVersion")), abiVersion);
        BitConverter.TryWriteBytes(blob.AsSpan(Off(tSnap, "Ok")), 1u);
        BitConverter.TryWriteBytes(blob.AsSpan(Off(tSnap, "Status")), 2u);   // Valid

        void PutTable(int at, ulong offset, ulong count)
        {
            BitConverter.TryWriteBytes(
                blob.AsSpan(at + Off(tTable, "Offset")), offset);
            BitConverter.TryWriteBytes(
                blob.AsSpan(at + Off(tTable, "Count")), count);
        }

        var sceneOff = Off(tSnap, "Scene");
        BitConverter.TryWriteBytes(blob.AsSpan(sceneOff + Off(tScene, "Ok")), 1u);
        PutTable(sceneOff + Off(tScene, "Roots"), (ulong)tableBase, 1);

        var childrenOff = Off(tNode, "Children");
        for (var level = 0; level < levels; ++level)
        {
            var levelBase = tableBase + level * branch * nodeSize;
            for (var slot = 0; slot < branch; ++slot)
            {
                if (level + 1 >= levels)
                {
                    continue;   // leaves: Children stays {0, 0}
                }

                var next = tableBase + (level + 1) * branch * nodeSize;
                PutTable(levelBase + slot * nodeSize + childrenOff, (ulong)next, (ulong)branch);
            }
        }

        return blob;
    }

    private static object Decode(byte[] blob)
    {
        var read = typeof(WozzitsEngineNativeClient).GetMethod(
            "ReadProjectSnapshot", BindingFlags.NonPublic | BindingFlags.Static)!;
        return Invoke(read, blob);
    }

    // The control: a well-formed chain still decodes. Without this, "it threw" is
    // indistinguishable from a broken harness.
    [Fact]
    public void AWellFormedChainDecodes()
    {
        var response = Decode(BuildSharedChildTableBlob(levels: 3, branch: 1));

        var scene = response.GetType().GetProperty("Scene")!.GetValue(response)!;
        var snapshot = scene.GetType().GetProperty("Snapshot")!.GetValue(scene)!;
        var roots = (System.Collections.ICollection)snapshot.GetType()
            .GetProperty("Roots")!.GetValue(snapshot)!;

        Assert.Equal(1, roots.Count);
    }

    // D3-C1. MaxSceneNodeDepth bounds DEPTH, not WORK: sibling records sharing one
    // child table stay under the cap and expand exponentially. Measured before the
    // fix -- 16 levels: 65,535 nodes / 174 ms; 24 levels: 16.7M nodes / 51 s / 8 GB;
    // 63 levels: still running at 31 s and 11.3 GB, heading for 2^63.
    //
    // The failure MUST be InvalidOperationException: that is the one type the
    // P/Invoke wrappers catch, so it becomes a failed load. OutOfMemoryException --
    // where this ended up -- is not, and terminates the editor.
    [Theory]
    [InlineData(16)]
    [InlineData(24)]
    [InlineData(63)]
    public void SharedChildTablesAreRefusedInsteadOfExpanding(int levels)
    {
        var blob = BuildSharedChildTableBlob(levels, branch: 2);

        var error = Assert.Throws<InvalidOperationException>(() => Decode(blob));

        Assert.Contains("table spans overlap", error.Message);
    }

    // The linear self-referential case the depth cap was originally added for.
    //
    // LOAD-BEARING PADDING: the decode budget charges one node stride per level, so
    // on a minimal blob it exhausts after a couple of levels and refuses this before
    // the depth cap ever fires -- leaving the depth cap untested. Sizing the blob
    // past MaxSceneNodeDepth strides is what keeps this test pinned to the guard it
    // names. Do not "simplify" the padding away.
    [Fact]
    public void ASelfReferentialChildTableIsRefusedByTheDepthCap()
    {
        var tSnap = T("WzEditorProjectSnapshotAbi");
        var tNode = T("WzEditorSceneNodeAbi");
        var nodeSize = Marshal.SizeOf(tNode);
        var maxDepth = (int)typeof(WozzitsEngineNativeClient)
            .GetField("MaxSceneNodeDepth", BindingFlags.NonPublic | BindingFlags.Static)!
            .GetRawConstantValue()!;

        var minimal = BuildSharedChildTableBlob(levels: 1, branch: 1);
        var blob = new byte[minimal.Length + (maxDepth + 8) * nodeSize];
        minimal.CopyTo(blob, 0);
        var tableBase = (Marshal.SizeOf(tSnap) + 7) / 8 * 8;

        // Point the single node's Children span at its own table.
        BitConverter.TryWriteBytes(
            blob.AsSpan(tableBase + Off(tNode, "Children") + 0), (ulong)tableBase);
        BitConverter.TryWriteBytes(
            blob.AsSpan(tableBase + Off(tNode, "Children") + 8), 1ul);

        var error = Assert.Throws<InvalidOperationException>(() => Decode(blob));

        Assert.Contains("deeper than", error.Message);
    }

    // D3-P053/P071: the SAME aliasing with NO recursion, so neither the depth cap
    // nor a scene-node cap can see it. N asset-graph node records all naming ONE
    // M-entry Params table decode N*M params -- every span individually in bounds.
    // At 1000 x 1000 the unbudgeted decoder materialises a million params and three
    // million strings; with the budget it refuses once the charged table bytes pass
    // what the response could contain.
    [Fact]
    public void SharedSubTablesInTheAssetGraphAreRefused()
    {
        var tSnap = T("WzEditorAssetGraphSnapshotAbi");
        var tNode = T("WzEditorAssetGraphNodeAbi");
        var tParam = T("WzEditorAssetGraphParamAbi");
        var tTable = T("WzEditorTableSpanAbi");

        const int nodes = 1000;
        const int paramsPerNode = 1000;
        var nodeSize = Marshal.SizeOf(tNode);
        var paramSize = Marshal.SizeOf(tParam);
        var nodeBase = (Marshal.SizeOf(tSnap) + 7) / 8 * 8;
        var paramBase = nodeBase + nodes * nodeSize;
        var blob = new byte[paramBase + paramsPerNode * paramSize];

        var abiVersion = (uint)T("WozzitsEngineAbi")
            .GetField("AbiVersion", BindingFlags.NonPublic | BindingFlags.Static)!
            .GetRawConstantValue()!;
        BitConverter.TryWriteBytes(blob.AsSpan(Off(tSnap, "AbiVersion")), abiVersion);
        BitConverter.TryWriteBytes(blob.AsSpan(Off(tSnap, "Ok")), 1u);

        void PutTable(int at, ulong offset, ulong count)
        {
            BitConverter.TryWriteBytes(blob.AsSpan(at + Off(tTable, "Offset")), offset);
            BitConverter.TryWriteBytes(blob.AsSpan(at + Off(tTable, "Count")), count);
        }

        PutTable(Off(tSnap, "Nodes"), (ulong)nodeBase, nodes);
        for (var index = 0; index < nodes; ++index)
        {
            PutTable(
                nodeBase + index * nodeSize + Off(tNode, "Params"),
                (ulong)paramBase,
                paramsPerNode);
        }

        var read = typeof(WozzitsEngineNativeClient).GetMethod(
            "ReadAssetGraphSnapshot",
            BindingFlags.NonPublic | BindingFlags.Static,
            [T("WzBuffer")]);
        var error = Assert.Throws<InvalidOperationException>(
            () => Invoke(read!, blob));

        Assert.Contains("table spans overlap", error.Message);
    }

    private static object Invoke(MethodInfo read, byte[] blob)
    {
        var tBuf = T("WzBuffer");
        var mem = Marshal.AllocHGlobal(blob.Length);
        try
        {
            Marshal.Copy(blob, 0, mem, blob.Length);
            var buffer = Activator.CreateInstance(tBuf)!;
            tBuf.GetField("Data")!.SetValue(buffer, mem);
            tBuf.GetField("Size")!.SetValue(buffer, (ulong)blob.Length);
            try
            {
                return read.Invoke(null, [buffer])!;
            }
            catch (TargetInvocationException tie)
            {
                throw tie.InnerException ?? tie;
            }
        }
        finally
        {
            Marshal.FreeHGlobal(mem);
        }
    }
}
