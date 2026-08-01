using System.Reflection;
using Wozzits.Editor.HostClient;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P074. `WozzitsEngineAbiLayout.AssertCurrent()` holds 253 assertions over all
/// 43 mirror structs — and no test ever executed a single one of them. It is
/// `[Conditional("DEBUG")]`, so a Release editor never runs it either; its only
/// caller is `EnsureResolverRegistered`, which every unit test avoids.
///
/// Nothing pinned the `AbiVersion` constant to the engine's `WZ_ABI_VERSION`
/// either, though a lockstep bump is a documented recurring mistake in this repo
/// and is the mechanism several D3 findings depend on.
/// </summary>
public sealed class AbiLockstepTests
{
    // Runs unconditionally: AssertCurrent is pure reflection over managed structs
    // and needs no engine build. Tests build Debug, so [Conditional("DEBUG")] is
    // live here — this is the one context in which those 253 assertions execute.
    [Fact]
    public void EveryMirrorStructMatchesItsDeclaredLayout()
    {
        var layout = typeof(WozzitsEngineNativeClient).Assembly
            .GetType("Wozzits.Editor.HostClient.WozzitsEngineAbiLayout")
            ?? throw new InvalidOperationException("missing WozzitsEngineAbiLayout");
        var assertCurrent = layout.GetMethod(
            "AssertCurrent", BindingFlags.NonPublic | BindingFlags.Static)
            ?? throw new InvalidOperationException("missing AssertCurrent");

        var failure = Record.Exception(() => assertCurrent.Invoke(null, null));

        Assert.Null(failure?.InnerException ?? failure);
    }

    // Pins the C# constant to the DLL the editor would actually load. This checks
    // the two sides against each other rather than against a hand-typed number,
    // which is the direction drift comes from: a C++-side bump with no C# change
    // passes every one of the 253 assertions above.
    //
    // HONEST LIMIT: with no engine build present this returns having checked
    // nothing, and xunit 2.9 has no runtime skip to say so. It is the test above
    // that is unconditional.
    [Fact]
    public void TheManagedAbiVersionMatchesTheBuiltEngine()
    {
        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(abiPath))
        {
            return;
        }

        var abi = typeof(WozzitsEngineNativeClient).Assembly
            .GetType("Wozzits.Editor.HostClient.WozzitsEngineAbi")!;
        var declared = (uint)abi
            .GetField("AbiVersion", BindingFlags.NonPublic | BindingFlags.Static)!
            .GetRawConstantValue()!;

        abi.GetMethod("EnsureResolverRegistered", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, null);
        var loaded = (uint)abi
            .GetMethod("WzAbiVersion", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, null)!;

        Assert.Equal(declared, loaded);
    }
}
