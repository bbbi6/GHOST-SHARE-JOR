using System;
using System.Diagnostics;
using System.IO;

static class AutoShare
{
    static void Main()
    {
        string dir = AppDomain.CurrentDomain.BaseDirectory;
        string app = Path.Combine(dir, "GhostShareJorApp.exe");
        if (!File.Exists(app)) return;

        if (Process.GetProcessesByName("GhostShareJorApp").Length > 0) return;

        Process.Start(new ProcessStartInfo
        {
            FileName = app,
            WorkingDirectory = dir,
            UseShellExecute = true
        });
    }
}
