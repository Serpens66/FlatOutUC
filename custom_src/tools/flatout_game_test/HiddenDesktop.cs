using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

internal static class FlatOutDesktopNative
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    internal struct Startup
    {
        public int Size;
        public string Reserved, Desktop, Title;
        public int X, Y, Width, Height, Cols, Rows, Fill, Flags;
        public short Show, ReservedSize;
        public IntPtr ReservedBytes, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct ProcessInfo
    {
        public IntPtr Process, Thread;
        public int ProcessId, ThreadId;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct JobLimits
    {
        public long ProcessTime, JobTime;
        public uint Flags;
        public UIntPtr MinWorkingSet, MaxWorkingSet;
        public uint ActiveProcesses;
        public UIntPtr Affinity;
        public uint Priority, Scheduling;
        public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    internal delegate bool EnumWindow(IntPtr window, IntPtr state);

    [DllImport("user32", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr mode, int flags, int access, IntPtr security);
    [DllImport("user32", SetLastError = true)] internal static extern IntPtr OpenInputDesktop(int flags, bool inherit, int access);
    [DllImport("user32", SetLastError = true)] internal static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern bool GetUserObjectInformation(IntPtr handle, int index, StringBuilder value, int size, out int needed);
    [DllImport("user32", SetLastError = true)] internal static extern bool EnumDesktopWindows(IntPtr desktop, EnumWindow callback, IntPtr state);
    [DllImport("user32")] internal static extern bool EnumChildWindows(IntPtr window, EnumWindow callback, IntPtr state);
    [DllImport("user32", CharSet = CharSet.Unicode)] internal static extern int GetClassName(IntPtr window, StringBuilder text, int size);
    [DllImport("user32", SetLastError = true, CharSet = CharSet.Unicode)]
    internal static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr length,
        StringBuilder value, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32")] internal static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32")] internal static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] internal static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32")] internal static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)]
    internal static extern bool CreateProcess(string application, StringBuilder command, IntPtr process, IntPtr thread,
        bool inherit, int flags, IntPtr environment, string workingDirectory, ref Startup startup, out ProcessInfo info);
    [DllImport("kernel32", SetLastError = true)] internal static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32", SetLastError = true)] internal static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32", SetLastError = true)] internal static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("kernel32", SetLastError = true)] internal static extern uint WaitForSingleObject(IntPtr handle, uint millis);
    [DllImport("kernel32", SetLastError = true)] internal static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32", SetLastError = true)] internal static extern IntPtr CreateJobObject(IntPtr security, IntPtr name);
    [DllImport("kernel32", SetLastError = true)]
    internal static extern bool SetInformationJobObject(IntPtr job, int type, ref JobLimits info, int size);
    [DllImport("kernel32", SetLastError = true)] internal static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32")] internal static extern uint SetErrorMode(uint mode);
}

public sealed class FlatOutDesktop : IDisposable
{
    private IntPtr desktop;
    private IntPtr job;
    private FlatOutDesktopNative.ProcessInfo game;
    public int Pid { get { return game.ProcessId; } }

    public static string InputDesktop()
    {
        IntPtr handle = FlatOutDesktopNative.OpenInputDesktop(0, false, 1);
        if (handle == IntPtr.Zero) throw new Win32Exception();
        try {
            var name = new StringBuilder(256);
            int needed;
            if (!FlatOutDesktopNative.GetUserObjectInformation(handle, 2, name, name.Capacity * 2, out needed))
                throw new Win32Exception();
            return name.ToString();
        } finally { FlatOutDesktopNative.CloseDesktop(handle); }
    }

    public static int ForegroundPid()
    {
        uint pid;
        FlatOutDesktopNative.GetWindowThreadProcessId(FlatOutDesktopNative.GetForegroundWindow(), out pid);
        return (int)pid;
    }

    public FlatOutDesktop(string name, string exe, string directory)
    {
        desktop = FlatOutDesktopNative.CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, 0xC3, IntPtr.Zero);
        if (desktop == IntPtr.Zero) throw new Win32Exception();
        var startup = new FlatOutDesktopNative.Startup();
        startup.Size = Marshal.SizeOf(typeof(FlatOutDesktopNative.Startup));
        startup.Desktop = "WinSta0\\" + name;
        var command = new StringBuilder("\"" + exe + "\"");
        uint mode = FlatOutDesktopNative.SetErrorMode(0x8003);
        try {
            if (!FlatOutDesktopNative.CreateProcess(exe, command, IntPtr.Zero, IntPtr.Zero, false, 4,
                    IntPtr.Zero, directory, ref startup, out game))
                throw new Win32Exception();
            job = FlatOutDesktopNative.CreateJobObject(IntPtr.Zero, IntPtr.Zero);
            if (job == IntPtr.Zero) throw new Win32Exception();
            var limits = new FlatOutDesktopNative.JobLimits { Flags = 0x2000 }; // kill on job close
            if (!FlatOutDesktopNative.SetInformationJobObject(job, 9, ref limits, Marshal.SizeOf(limits)) ||
                !FlatOutDesktopNative.AssignProcessToJobObject(job, game.Process))
                throw new Win32Exception();
        } catch {
            Dispose();
            throw;
        } finally { FlatOutDesktopNative.SetErrorMode(mode); }
    }

    public void Resume()
    {
        if (FlatOutDesktopNative.ResumeThread(game.Thread) == uint.MaxValue) throw new Win32Exception();
    }

    public bool HasExited { get { return WaitForExit(0); } }

    public uint ExitCode
    {
        get {
            uint code;
            if (!HasExited) throw new InvalidOperationException("The test game is still running.");
            if (!FlatOutDesktopNative.GetExitCodeProcess(game.Process, out code)) throw new Win32Exception();
            return code;
        }
    }

    public bool WaitForExit(uint milliseconds)
    {
        uint result = FlatOutDesktopNative.WaitForSingleObject(game.Process, milliseconds);
        if (result == uint.MaxValue) throw new Win32Exception();
        return result == 0;
    }

    public bool HasGameWindow()
    {
        bool found = false;
        FlatOutDesktopNative.EnumDesktopWindows(desktop, delegate(IntPtr window, IntPtr state) {
            uint pid;
            FlatOutDesktopNative.Rect rect;
            FlatOutDesktopNative.GetWindowThreadProcessId(window, out pid);
            if (pid == Pid && FlatOutDesktopNative.IsWindowVisible(window) &&
                FlatOutDesktopNative.GetClientRect(window, out rect) &&
                rect.Right - rect.Left >= 320 && rect.Bottom - rect.Top >= 200) found = true;
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public string GameDialog()
    {
        string found = null;
        FlatOutDesktopNative.EnumDesktopWindows(desktop, delegate(IntPtr window, IntPtr state) {
            uint pid;
            FlatOutDesktopNative.GetWindowThreadProcessId(window, out pid);
            if (pid != Pid) return true;
            var className = new StringBuilder(80);
            FlatOutDesktopNative.GetClassName(window, className, className.Capacity);
            if (className.ToString() != "#32770" || !FlatOutDesktopNative.IsWindowVisible(window)) return true;
            var contents = new StringBuilder();
            FlatOutDesktopNative.EnumChildWindows(window, delegate(IntPtr child, IntPtr unused) {
                uint owner;
                FlatOutDesktopNative.GetWindowThreadProcessId(child, out owner);
                if (owner != pid) return true;
                var value = new StringBuilder(2048);
                IntPtr result;
                if (FlatOutDesktopNative.SendMessageTimeout(child, 0x000d, new IntPtr(value.Capacity),
                        value, 2, 75, out result) != IntPtr.Zero && value.Length > 0)
                    contents.Append(value).Append(' ');
                return true;
            }, IntPtr.Zero);
            found = contents.ToString().Trim();
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public void Dispose()
    {
        if (game.Process != IntPtr.Zero) {
            if (!HasExited) {
                FlatOutDesktopNative.TerminateProcess(game.Process, 1);
                if (!WaitForExit(5000))
                    throw new InvalidOperationException("The test game did not exit.");
            }
            FlatOutDesktopNative.CloseHandle(game.Process);
            FlatOutDesktopNative.CloseHandle(game.Thread);
            game = new FlatOutDesktopNative.ProcessInfo();
        }
        if (desktop != IntPtr.Zero) {
            FlatOutDesktopNative.CloseDesktop(desktop);
            desktop = IntPtr.Zero;
        }
        if (job != IntPtr.Zero) {
            FlatOutDesktopNative.CloseHandle(job);
            job = IntPtr.Zero;
        }
    }
}
