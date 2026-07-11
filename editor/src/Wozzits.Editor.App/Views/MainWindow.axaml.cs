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

    // Re-query the engine for its scenelets whenever the Prefabs menu opens, so the
    // list is current even if the viewport was still loading at construction (the
    // catalog is published once the runtime finishes its first load) or was
    // restarted. Scenelets is an ObservableCollection, so the bound submenu updates.
    private void OnPrefabsMenuOpened(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel)
        {
            return;
        }
        viewModel.RefreshSceneletsCommand.Execute(null);

        // Build the "Open Prefab" submenu items in code (like the inspector's
        // behavior "+" flyout): a bound ItemsSource populated late renders an empty
        // submenu, so set the items directly, before the submenu is shown.
        OpenPrefabMenu.Items.Clear();
        foreach (var scenelet in viewModel.Scenelets)
        {
            OpenPrefabMenu.Items.Add(new MenuItem
            {
                Header = scenelet.Name,
                Command = viewModel.OpenSceneletCommand,
                CommandParameter = scenelet,
            });
        }
    }

    // Populate the Statecharts submenu with the project's authored charts each time it
    // opens (mirrors the Prefabs menu). Selecting one opens its dataflow canvas.
    private void OnStatechartsMenuOpened(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel)
        {
            return;
        }

        viewModel.RefreshStatechartsCommand.Execute(null);

        OpenStatechartMenu.Items.Clear();
        OpenControlMenu.Items.Clear();
        foreach (var chart in viewModel.Statecharts)
        {
            OpenStatechartMenu.Items.Add(new MenuItem
            {
                Header = chart.Name,
                Command = viewModel.OpenStatechartDataflowCommand,
                CommandParameter = chart,
            });
            OpenControlMenu.Items.Add(new MenuItem
            {
                Header = chart.Name,
                Command = viewModel.OpenStatechartControlCommand,
                CommandParameter = chart,
            });
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
