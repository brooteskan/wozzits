using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.App.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Closed += OnClosed;
        SyncThemeMenu();
    }

    public MainWindow(MainWindowViewModel viewModel)
    {
        DataContext = viewModel;
        InitializeComponent();
        Closed += OnClosed;
        SyncThemeMenu();
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.Shutdown();
        }
    }

    private void OnSelectBlueTheme(object? sender, RoutedEventArgs e)
        => SetTheme(EditorTheme.Variant.Blue);

    private void OnSelectRedTheme(object? sender, RoutedEventArgs e)
        => SetTheme(EditorTheme.Variant.Red);

    private void SetTheme(EditorTheme.Variant variant)
    {
        EditorTheme.Apply(variant);
        SyncThemeMenu();
    }

    // Reflect the active theme in the radio checks (keeps them correct even when
    // the user re-clicks the already-active item).
    private void SyncThemeMenu()
    {
        BlueThemeMenuItem.IsChecked = EditorTheme.Current == EditorTheme.Variant.Blue;
        RedThemeMenuItem.IsChecked = EditorTheme.Current == EditorTheme.Variant.Red;
    }
}
