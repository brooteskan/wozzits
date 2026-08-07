namespace Wozzits.Editor.App.Views;

using System;
using System.IO;
using System.Text.Json;

// The main window's placement across runs: position, size, and maximized/fullscreen
// state, so the editor reopens where it was last closed -- same display, same size and
// state. Stored per-user under LocalApplicationData; an absent or corrupt file just
// means "use the default placement", never an error.
public sealed record WindowLayout(int X, int Y, int Width, int Height, string State);

public static class WindowStateStore
{
    private static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Wozzits", "editor-window.json");

    public static WindowLayout? Load()
    {
        try
        {
            return File.Exists(FilePath)
                ? JsonSerializer.Deserialize<WindowLayout>(File.ReadAllText(FilePath))
                : null;
        }
        catch
        {
            return null;
        }
    }

    public static void Save(WindowLayout layout)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(layout));
        }
        catch
        {
            // Best-effort: failing to remember placement is never worth an error.
        }
    }
}
