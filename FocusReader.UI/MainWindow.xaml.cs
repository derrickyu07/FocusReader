using System.ComponentModel;
using System.Diagnostics;
using System.Windows;

namespace FocusReader.UI;

public partial class MainWindow : Window
{
    private const int MaxLogEntries = 200;

    // IMPORTANT: keep the delegate in a field.
    // The C++ side only holds a raw function pointer, which the .NET garbage collector
    // can't see. If this were a local variable, the GC could free the delegate while
    // C++ still calls it, and the app would crash at a random later moment.
    private readonly NativeMethods.AnnounceCallback _callback;

    public MainWindow()
    {
        // turns xaml into real object, it grabs the information from location
        //
        InitializeComponent(); 
        _callback = OnAnnounce;
        NativeMethods.Engine_RegisterCallback(_callback);
    }

    // Called by C++ on UIA's background thread, NOT the UI thread.
    // WPF controls may only be touched from the UI thread, so we hand the work over
    // with BeginInvoke (queue it and return immediately; never wait inside a callback).
    private void OnAnnounce(string text)
    {
        Dispatcher.BeginInvoke(() =>
        {
            LogList.Items.Insert(0, text);
            if (LogList.Items.Count > MaxLogEntries)
                LogList.Items.RemoveAt(LogList.Items.Count - 1);
        });
    }

    private void EngineToggle_Checked(object sender, RoutedEventArgs e)
    {
        int hr = NativeMethods.Engine_Start(); //start the focus engine and get result
        if (hr < 0) //if 0 is Success and 1 and failure
        {
            StatusText.Text = $"Failed to start (0x{hr:X8}).";
            EngineToggle.IsChecked = false;
            return;
        }
        NativeMethods.Engine_SetRate((int)RateSlider.Value); //sending the rate to c++
        EngineToggle.Content = "Stop reading";
        StatusText.Text = "Running. Tab around any app.";
    }

    private void EngineToggle_Unchecked(object sender, RoutedEventArgs e)
    {
        NativeMethods.Engine_Stop(); //stop the focus engine
        EngineToggle.Content = "Start reading";
        StatusText.Text = "Stopped.";
    }

    private void RateSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (RateText is null) return; // fires once during InitializeComponent, before controls exist
        int rate = (int)e.NewValue; //case to int
        RateText.Text = rate.ToString(); //converts int to string
        NativeMethods.Engine_SetRate(rate); //sending the rate to the focus engine
    }

    private void Window_Closing(object? sender, CancelEventArgs e)
    {
        NativeMethods.Engine_Stop();                 // stop events first
        NativeMethods.Engine_RegisterCallback(null); // then make sure C++ won't call us again
    }
}