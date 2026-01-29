using System;
using System.Windows;
using DNFSyncBox;

namespace DNFSyncBox.Views;

public partial class SyncView : System.Windows.Controls.UserControl
{
    private readonly SyncController _syncController = new();
    private bool _started;

    public SyncView()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_started)
        {
            return;
        }
        _started = true;
        _syncController.StatusChanged += HandleStatusChanged;
        _syncController.LogAdded += HandleLogAdded;
        _syncController.Start();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        if (!_started)
        {
            return;
        }
        _started = false;
        _syncController.Dispose();
    }

    private void HandleStatusChanged(SyncStatus status)
    {
        Dispatcher.Invoke(() =>
        {
            var pauseLabel = status.IsPaused ? "已暂停" : "同步中";
            if (status.IsAutoPaused)
            {
                pauseLabel += "（自动暂停）";
            }

            PauseText.Text = $"状态：{pauseLabel}";
            WindowCountText.Text = $"窗口：{status.TotalCount}（从控 {status.SlaveCount}）";
            MasterText.Text = status.MasterHandle == IntPtr.Zero
                ? "主控：无"
                : $"主控：0x{status.MasterHandle.ToInt64():X}";
            ForegroundText.Text = $"前台 DNF：{(status.ForegroundIsDnf ? "是" : "否")}";
        });
    }

    private void HandleLogAdded(string message)
    {
        Dispatcher.Invoke(() =>
        {
            LogList.Items.Insert(0, message);
            while (LogList.Items.Count > 200)
            {
                LogList.Items.RemoveAt(LogList.Items.Count - 1);
            }
        });
    }

    private void OnTogglePause(object sender, RoutedEventArgs e)
    {
        _syncController.TogglePause();
    }

    private void OnRefresh(object sender, RoutedEventArgs e)
    {
        _syncController.RefreshWindows();
    }
}
