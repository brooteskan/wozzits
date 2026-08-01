using System.Reflection;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P065. D3-C5 fixed the nine transform boxes; these two siblings never got the
/// same guard. float.TryParse accepts "NaN" and "Infinity" BY NAME and returns true
/// with +inf for "1e39" -- an ordinary fat-fingered exponent, not hostile input.
///
/// The two surfaces fail differently, so they are guarded differently:
///   - the motion fields round-trip through the JSON token `null`, which the
///     engine's per-field read_number rejects as "absent", so the field silently
///     reverts to its struct default on the next project open;
///   - the renderable constants go into a GPU constant tail and reach the shader.
/// </summary>
public sealed class InspectorNonFiniteFieldTests
{
    private static float ParseFloatOrZero(string text)
    {
        var parse = typeof(InspectorPaneViewModel).GetMethod(
            "ParseFloatOrZero", BindingFlags.NonPublic | BindingFlags.Static)!;
        return (float)parse.Invoke(null, [text])!;
    }

    // 1e39 is the case that makes this a real bug rather than a hostile-input
    // exercise: it is a plausible typo, and float.TryParse reports SUCCESS for it.
    [Theory]
    [InlineData("NaN")]
    [InlineData("Infinity")]
    [InlineData("-Infinity")]
    [InlineData("1e39")]
    [InlineData("-1e39")]
    public void AMotionFieldRefusesANonFiniteValue(string text)
    {
        Assert.Equal(0f, ParseFloatOrZero(text));
    }

    // The calibration half: the guard must not eat legitimate values. 3.4e38 is
    // just inside float range and MUST survive.
    [Theory]
    [InlineData("0.75", 0.75f)]
    [InlineData("-12", -12f)]
    [InlineData("3.4e38", 3.4e38f)]
    public void AMotionFieldStillAcceptsALegitimateValue(string text, float expected)
    {
        Assert.Equal(expected, ParseFloatOrZero(text));
    }

    private static InspectorRenderableConstantRowViewModel Row(List<float[]> applied)
    {
        return new InspectorRenderableConstantRowViewModel(
            "tint",
            width: 4,
            typeLabel: "Color",
            initialValue: null,
            apply: (_, value) => applied.Add(value ?? []));
    }

    // The control: an ordinary edit DOES apply, so "nothing was applied" below
    // means the guard fired rather than the harness being wired up wrong.
    [Fact]
    public void AFiniteConstantComponentIsApplied()
    {
        var applied = new List<float[]>();

        Row(applied).ValueX = "0.5";

        Assert.Equal(0.5f, Assert.Single(applied)[0]);
    }

    [Theory]
    [InlineData("NaN")]
    [InlineData("Infinity")]
    [InlineData("1e39")]
    public void ANonFiniteConstantComponentIsNotApplied(string text)
    {
        var applied = new List<float[]>();

        Row(applied).ValueX = text;

        Assert.Empty(applied);
    }
}
