using System.Runtime.InteropServices;

namespace FocusReader.UI;

// P/Invoke declarations: each [DllImport] tells .NET "this method lives in FocusEngine.dll".
// The signatures must match the C++ exports exactly, or calls will crash or misbehave.
internal static class NativeMethods
{
    private const string Dll = "FocusEngine.dll";

    // Matches: typedef void(__stdcall* AnnounceCallback)(const wchar_t* text);
    // LPWStr = a UTF-16 C string. .NET copies it into a normal C# string for us.
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    public delegate void AnnounceCallback([MarshalAs(UnmanagedType.LPWStr)] string text);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    public static extern int Engine_Start();

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    public static extern void Engine_Stop();

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    public static extern void Engine_SetRate(int rate);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    public static extern void Engine_RegisterCallback(AnnounceCallback? callback);
}