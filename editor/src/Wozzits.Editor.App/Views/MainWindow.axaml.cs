using System;
using System.Linq;
using Avalonia;
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
        HookWindowPlacement();
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

    // --- window placement persistence (position / size / maximized state across runs) ---

    private PixelPoint _normalPosition;
    private Size _normalSize;

    // Restore the last-closed placement, keep the normal (un-maximized) bounds current so
    // a maximized window still knows where to sit when un-maximized, and save on close.
    private void HookWindowPlacement()
    {
        _normalPosition = Position;
        _normalSize = new Size(Width, Height);
        RestoreWindowPlacement();
        PositionChanged += (_, _) => RememberNormalBounds();
        SizeChanged += (_, _) => RememberNormalBounds();
        Opened += (_, _) => EnsureOnScreen();
        Closing += (_, _) => SaveWindowPlacement();
    }

    private void RememberNormalBounds()
    {
        if (WindowState == WindowState.Normal)
        {
            _normalPosition = Position;
            _normalSize = new Size(Width, Height);
        }
    }

    private void RestoreWindowPlacement()
    {
        if (WindowStateStore.Load() is not { } saved)
        {
            return;
        }
        WindowStartupLocation = WindowStartupLocation.Manual;
        Width = Math.Max(MinWidth, saved.Width);
        Height = Math.Max(MinHeight, saved.Height);
        Position = new PixelPoint(saved.X, saved.Y);
        _normalPosition = Position;
        _normalSize = new Size(Width, Height);
        if (Enum.TryParse<WindowState>(saved.State, out var state)
            && state is WindowState.Maximized or WindowState.FullScreen)
        {
            WindowState = state;
        }
    }

    // If the saved position lands off every connected screen (a monitor was unplugged or
    // rearranged), pull the window onto the primary one so it can't be lost off-screen.
    private void EnsureOnScreen()
    {
        var screens = Screens.All;
        if (screens.Count == 0)
        {
            return;
        }
        var frame = new PixelRect(Position, PixelSize.FromSize(_normalSize, RenderScaling));
        if (!screens.Any(s => s.Bounds.Intersects(frame)))
        {
            Position = (Screens.Primary ?? screens[0]).WorkingArea.Position;
        }
    }

    private void SaveWindowPlacement()
    {
        var state = WindowState == WindowState.Minimized ? WindowState.Normal : WindowState;
        // Maximized/fullscreen: the current Position is on the target monitor (so it
        // re-maximizes there); pair it with the last NORMAL size for a later un-maximize.
        var size = state == WindowState.Normal ? new Size(Width, Height) : _normalSize;
        WindowStateStore.Save(new WindowLayout(
            Position.X, Position.Y, (int)size.Width, (int)size.Height, state.ToString()));
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
        foreach (var chart in viewModel.Statecharts)
        {
            OpenStatechartMenu.Items.Add(new MenuItem
            {
                Header = chart.Name,
                Command = viewModel.OpenStatechartCommand,
                CommandParameter = chart,
            });
        }
    }

    // Populate the Minds submenu with the project's authored .mind.json graphs each time it
    // opens (mirrors the Statecharts menu). Selecting one opens its graph canvas.
    private void OnMindsMenuOpened(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel)
        {
            return;
        }

        viewModel.RefreshMindsCommand.Execute(null);

        OpenMindMenu.Items.Clear();
        foreach (var mind in viewModel.Minds)
        {
            OpenMindMenu.Items.Add(new MenuItem
            {
                Header = mind.Name,
                Command = viewModel.OpenMindCommand,
                CommandParameter = mind,
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
