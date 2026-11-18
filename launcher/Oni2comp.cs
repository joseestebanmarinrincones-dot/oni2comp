using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace Oni2comp
{
    // Native launcher: starts the WinForms app (app\app.ps1) without a console
    // window and keeps this process alive so the app window owns the taskbar
    // entry. The launcher contains no game code: the app builds the runner from
    // the disc image the user owns.
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            string root = AppDomain.CurrentDomain.BaseDirectory;
            string script = Path.Combine(root, Path.Combine("app", "app.ps1"));
            if (!File.Exists(script))
            {
                string parent = Path.GetDirectoryName(root.TrimEnd(Path.DirectorySeparatorChar));
                if (!String.IsNullOrEmpty(parent))
                {
                    string candidate = Path.Combine(parent, Path.Combine("app", "app.ps1"));
                    if (File.Exists(candidate))
                    {
                        root = parent;
                        script = candidate;
                    }
                }
            }

            if (!File.Exists(script))
            {
                MessageBox.Show(
                    "No se encuentra app\\app.ps1 junto a Oni2comp.exe.\n\n" +
                    "Extrae la carpeta completa del ZIP y ejecuta Oni2comp.exe desde ella.",
                    "Oni2comp", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            string powershell = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                Path.Combine("System32", Path.Combine("WindowsPowerShell", Path.Combine("v1.0", "powershell.exe"))));
            if (!File.Exists(powershell))
            {
                MessageBox.Show(
                    "No se encuentra Windows PowerShell 5.1 en este equipo.",
                    "Oni2comp", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            ProcessStartInfo info = new ProcessStartInfo();
            info.FileName = powershell;
            info.Arguments = "-NoLogo -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + "\"";
            info.WorkingDirectory = root;
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            info.WindowStyle = ProcessWindowStyle.Hidden;
            // The portable package is a release: do not replace its files with an
            // upstream build while the player is using it. Updates come from a new
            // Oni2comp package.
            info.EnvironmentVariables["PS2APP_NO_UPDATE_CHECK"] = "1";

            try
            {
                using (Process process = Process.Start(info))
                {
                    if (process != null) process.WaitForExit();
                }
            }
            catch (Exception error)
            {
                MessageBox.Show(
                    "No se pudo abrir Oni2comp.\n\n" + error.Message,
                    "Oni2comp", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
