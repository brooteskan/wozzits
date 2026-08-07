using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Wozzits.Editor.App.Views;

// A minimal modal text prompt: a message + a single-line input, returned via
// ShowDialog<string?> (the trimmed text on OK/Enter, null on Cancel/Esc). Reusable;
// the "New Scenelet" flow uses it to name a scenelet up front.
public partial class TextPromptWindow : Window
{
    public TextPromptWindow()
    {
        InitializeComponent();
    }

    public TextPromptWindow(string title, string prompt, string initial = "")
        : this()
    {
        Title = title;
        PromptText.Text = prompt;
        Input.Text = initial;
        // Land in the field with the suggested name selected, so typing replaces it.
        Opened += (_, _) =>
        {
            Input.SelectAll();
            Input.Focus();
        };
    }

    private void OnOk(object? sender, RoutedEventArgs e) =>
        Close((Input.Text ?? string.Empty).Trim());

    private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);
}
