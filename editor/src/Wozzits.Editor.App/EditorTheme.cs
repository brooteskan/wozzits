using System;
using Avalonia;
using Avalonia.Markup.Xaml.Styling;

namespace Wozzits.Editor.App;

/// <summary>
/// Runtime editor color theme. Each theme is a <see cref="ResourceDictionary"/>
/// of Wozzits* color/brush keys that the styles bind via DynamicResource; applying
/// a theme swaps the merged dictionary in <c>Application.Resources</c>, so the UI
/// re-themes live without a restart.
/// </summary>
public static class EditorTheme
{
    public enum Variant
    {
        /// <summary>Daylight: the original Solarized-dark cyan palette.</summary>
        Blue,

        /// <summary>Night vision: muted reds for dark-adapted (telescope) use.</summary>
        Red,
    }

    private static readonly Uri BlueUri =
        new("avares://Wozzits.Editor.App/Themes/BlueTheme.axaml");

    private static readonly Uri RedUri =
        new("avares://Wozzits.Editor.App/Themes/RedTheme.axaml");

    public static Variant Current { get; private set; } = Variant.Blue;

    public static void Apply(Variant variant)
    {
        var app = Application.Current;
        if (app is null)
        {
            return;
        }

        // Drop whichever theme dictionary is currently merged (including the one
        // App.axaml included by default) and merge the requested one in its place.
        var merged = app.Resources.MergedDictionaries;
        for (var i = merged.Count - 1; i >= 0; i--)
        {
            if (merged[i] is ResourceInclude existing
                && (existing.Source == BlueUri || existing.Source == RedUri))
            {
                merged.RemoveAt(i);
            }
        }

        var uri = variant == Variant.Red ? RedUri : BlueUri;
        merged.Add(new ResourceInclude(uri) { Source = uri });
        Current = variant;
    }
}
