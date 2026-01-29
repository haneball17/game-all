using System.Windows.Controls;
using System.Windows.Input;
using GameHelperGUI.ViewModels;

namespace GameHelperGUI.Views;

public partial class HelperView : UserControl
{
    public HelperView()
    {
        InitializeComponent();
        if (DataContext == null)
        {
            DataContext = new MainViewModel();
        }
        Loaded += (_, _) => Focus();
    }

    protected override void OnPreviewKeyDown(KeyEventArgs e)
    {
        if (DataContext is MainViewModel viewModel && viewModel.HandleHotkeyInput(e.Key))
        {
            e.Handled = true;
        }
        base.OnPreviewKeyDown(e);
    }
}
