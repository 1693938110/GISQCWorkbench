using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace GISQCInstaller
{
    static class Program
    {
        public const string ProductName = "GIS 数据质量检查工作台";
        public const string ProductId = "GISQCWorkbench";
        public const string MainExeName = "GISQCWorkbench.exe";
        public const string UninstallerName = "Uninstall-GISQCWorkbench.exe";
        public const string ShortcutName = "GIS数据质量检查工作台.lnk";
        public const string RegistryKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\GISQCWorkbench";

        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                if (args.Contains("/uninstall", StringComparer.OrdinalIgnoreCase))
                {
                    string uninstallDir = ArgValue(args, "/dir") ?? AppDomain.CurrentDomain.BaseDirectory;
                    bool silentUninstall = args.Contains("/silent", StringComparer.OrdinalIgnoreCase) || args.Contains("/S", StringComparer.OrdinalIgnoreCase);
                    InstallerCore.Uninstall(uninstallDir, silentUninstall);
                    return 0;
                }

                string existingDir = InstallerCore.ExistingInstallLocation();
                string defaultDir = !string.IsNullOrWhiteSpace(existingDir)
                    ? existingDir
                    : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), ProductId);
                string installDir = args.FirstOrDefault(a => !string.IsNullOrWhiteSpace(a) && !a.StartsWith("/")) ?? defaultDir;
                if (args.Contains("/silent", StringComparer.OrdinalIgnoreCase) || args.Contains("/S", StringComparer.OrdinalIgnoreCase))
                {
                    InstallerCore.Install(installDir, null);
                    return 0;
                }
                Application.Run(new InstallerWizard(defaultDir));
                return 0;
            }
            catch (Exception ex)
            {
                MessageBox.Show("安装失败：\n" + ex.Message, ProductName + "安装程序", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 2;
            }
        }

        static string ArgValue(string[] args, string name)
        {
            for (int i = 0; i < args.Length - 1; ++i)
            {
                if (string.Equals(args[i], name, StringComparison.OrdinalIgnoreCase)) return args[i + 1];
            }
            return null;
        }

        public static Icon LoadAppIcon()
        {
            try
            {
                using (var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("app.ico"))
                {
                    if (input != null) return new Icon(input);
                }
            }
            catch { }
            return SystemIcons.Application;
        }
    }

    sealed class InstallerWizard : Form
    {
        readonly string defaultDir;
        readonly Label title = new Label();
        readonly Label body = new Label();
        readonly TextBox pathBox = new TextBox();
        readonly Button browseButton = new Button();
        readonly Button backButton = new Button();
        readonly Button nextButton = new Button();
        readonly Button cancelButton = new Button();
        readonly ProgressBar progress = new ProgressBar();
        readonly CheckBox launchCheck = new CheckBox();
        readonly string existingDir;
        int page = 0;
        bool installing = false;

        public InstallerWizard(string defaultDir)
        {
            this.defaultDir = defaultDir;
            existingDir = InstallerCore.ExistingInstallLocation();
            Text = Program.ProductName + " 安装向导";
            Icon = Program.LoadAppIcon();
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            ClientSize = new Size(640, 420);
            Font = new Font("Microsoft YaHei UI", 9F);

            var banner = new Panel { Left = 0, Top = 0, Width = 640, Height = 76, BackColor = Color.FromArgb(19, 132, 202) };
            var iconBox = new PictureBox { Left = 22, Top = 14, Width = 48, Height = 48, SizeMode = PictureBoxSizeMode.StretchImage, Image = Program.LoadAppIcon().ToBitmap() };
            var bannerTitle = new Label { Left = 84, Top = 18, Width = 520, Height = 34, Text = Program.ProductName, ForeColor = Color.White, Font = new Font("Microsoft YaHei UI", 16F, FontStyle.Bold) };
            var bannerSub = new Label { Left = 86, Top = 50, Width = 520, Height = 20, Text = "安装向导", ForeColor = Color.WhiteSmoke };
            banner.Controls.Add(iconBox); banner.Controls.Add(bannerTitle); banner.Controls.Add(bannerSub); Controls.Add(banner);

            title.Left = 28; title.Top = 98; title.Width = 580; title.Height = 28; title.Font = new Font("Microsoft YaHei UI", 12F, FontStyle.Bold); Controls.Add(title);
            body.Left = 28; body.Top = 140; body.Width = 580; body.Height = 110; body.AutoSize = false; Controls.Add(body);
            pathBox.Left = 28; pathBox.Top = 260; pathBox.Width = 470; pathBox.Height = 28; Controls.Add(pathBox);
            browseButton.Left = 510; browseButton.Top = 258; browseButton.Width = 90; browseButton.Height = 30; browseButton.Text = "浏览..."; browseButton.Click += Browse; Controls.Add(browseButton);
            progress.Left = 28; progress.Top = 270; progress.Width = 572; progress.Height = 24; progress.Style = ProgressBarStyle.Marquee; progress.Visible = false; Controls.Add(progress);
            launchCheck.Left = 28; launchCheck.Top = 260; launchCheck.Width = 400; launchCheck.Text = "完成后立即启动软件"; launchCheck.Checked = true; launchCheck.Visible = false; Controls.Add(launchCheck);

            var line = new Panel { Left = 0, Top = 338, Width = 640, Height = 1, BackColor = Color.Gainsboro }; Controls.Add(line);
            backButton.Left = 330; backButton.Top = 362; backButton.Width = 88; backButton.Height = 30; backButton.Text = "上一步"; backButton.Click += (s, e) => { if (page > 0) { page--; Render(); } }; Controls.Add(backButton);
            nextButton.Left = 424; nextButton.Top = 362; nextButton.Width = 88; nextButton.Height = 30; nextButton.Text = "下一步"; nextButton.Click += Next; Controls.Add(nextButton);
            cancelButton.Left = 518; cancelButton.Top = 362; cancelButton.Width = 88; cancelButton.Height = 30; cancelButton.Text = "取消"; cancelButton.Click += (s, e) => Close(); Controls.Add(cancelButton);

            pathBox.Text = defaultDir;
            Render();
        }

        void Render()
        {
            pathBox.Visible = browseButton.Visible = progress.Visible = launchCheck.Visible = false;
            backButton.Enabled = page > 0 && !installing;
            cancelButton.Enabled = !installing;
            nextButton.Enabled = !installing;
            if (page == 0)
            {
                title.Text = "欢迎使用安装向导";
                if (!string.IsNullOrWhiteSpace(existingDir))
                {
                    body.Text = "检测到本机已安装 " + Program.ProductName + "：\r\n" + existingDir +
                                "\r\n\r\n继续安装将执行覆盖升级，并保留授权文件和用户数据。";
                }
                else
                {
                    body.Text = "本向导将把 " + Program.ProductName + " 安装到您的电脑。\r\n\r\n点击“下一步”继续，或点击“取消”退出安装。";
                }
                nextButton.Text = "下一步";
            }
            else if (page == 1)
            {
                title.Text = "选择安装位置";
                body.Text = "请选择软件安装目录。建议保持默认路径；如需安装到其他位置，请点击“浏览”。";
                pathBox.Visible = browseButton.Visible = true;
                nextButton.Text = "下一步";
            }
            else if (page == 2)
            {
                title.Text = "准备安装";
                body.Text = "即将安装到：\r\n" + pathBox.Text + "\r\n\r\n点击“安装”开始复制文件。";
                nextButton.Text = "安装";
            }
            else if (page == 3)
            {
                title.Text = "正在安装";
                body.Text = "正在复制程序文件和运行库，请稍候...";
                progress.Visible = true;
                nextButton.Text = "下一步";
                backButton.Enabled = nextButton.Enabled = cancelButton.Enabled = false;
            }
            else
            {
                title.Text = "安装完成";
                body.Text = Program.ProductName + " 已安装完成。\r\n\r\n已创建桌面快捷方式和开始菜单快捷方式，\r\n并已写入 Windows 控制面板/设置中的卸载入口。";
                launchCheck.Visible = true;
                backButton.Enabled = false;
                cancelButton.Text = "关闭";
                cancelButton.Enabled = true;
                nextButton.Text = "完成";
                nextButton.Enabled = true;
            }
        }

        void Browse(object sender, EventArgs e)
        {
            using (var dlg = new FolderBrowserDialog { Description = "选择安装目录", SelectedPath = Directory.Exists(pathBox.Text) ? pathBox.Text : defaultDir })
            {
                if (dlg.ShowDialog(this) == DialogResult.OK) pathBox.Text = dlg.SelectedPath;
            }
        }

        void Next(object sender, EventArgs e)
        {
            if (page < 2) { page++; Render(); return; }
            if (page == 2) { BeginInstall(); return; }
            if (page >= 4)
            {
                if (launchCheck.Checked)
                {
                    var exe = Path.Combine(pathBox.Text, Program.MainExeName);
                    if (File.Exists(exe)) Process.Start(exe);
                }
                Close();
            }
        }

        void BeginInstall()
        {
            if (!string.IsNullOrWhiteSpace(existingDir) && Directory.Exists(existingDir))
            {
                var result = MessageBox.Show(this,
                    "检测到已安装版本。\r\n\r\n安装程序将覆盖升级到：\r\n" + pathBox.Text +
                    "\r\n\r\n授权文件会保留到用户目录。是否继续？",
                    Text,
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Question,
                    MessageBoxDefaultButton.Button1);
                if (result != DialogResult.Yes) return;
            }
            page = 3; installing = true; Render();
            string targetPath = pathBox.Text;
            var worker = new Thread(() =>
            {
                try
                {
                    InstallerCore.Install(targetPath, msg => BeginInvoke((Action)(() => body.Text = msg)));
                    BeginInvoke((Action)(() => { installing = false; page = 4; Render(); }));
                }
                catch (Exception ex)
                {
                    BeginInvoke((Action)(() =>
                    {
                        installing = false; Render();
                        MessageBox.Show(this, "安装失败：\r\n" + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }));
                }
            });
            worker.IsBackground = true;
            worker.Start();
        }
    }

    static class InstallerCore
    {
        public static string ExistingInstallLocation()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(Program.RegistryKeyPath))
                {
                    var value = key == null ? null : key.GetValue("InstallLocation") as string;
                    if (!string.IsNullOrWhiteSpace(value)) return value;
                }
            }
            catch { }
            return null;
        }

        public static void Install(string installDir, Action<string> status)
        {
            if (string.IsNullOrWhiteSpace(installDir)) throw new InvalidOperationException("安装路径不能为空。");
            string target = Path.GetFullPath(installDir);
            if (status != null) status("正在准备安装目录：\r\n" + target);
            if (Directory.Exists(target))
            {
                PreserveLicenseFile(target);
                foreach (var file in Directory.GetFiles(target, "*", SearchOption.AllDirectories)) File.SetAttributes(file, FileAttributes.Normal);
                Directory.Delete(target, true);
            }
            Directory.CreateDirectory(target);

            string tempZip = Path.Combine(Path.GetTempPath(), "GISQCWorkbench_payload_" + Guid.NewGuid().ToString("N") + ".zip");
            using (var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
            {
                if (input == null) throw new InvalidOperationException("安装包内未找到 payload.zip");
                using (var output = File.Create(tempZip)) input.CopyTo(output);
            }
            try
            {
                if (status != null) status("正在复制程序文件和依赖库，请稍候...");
                ZipFile.ExtractToDirectory(tempZip, target, Encoding.UTF8);
            }
            finally { try { File.Delete(tempZip); } catch { } }

            if (status != null) status("正在创建快捷方式、卸载程序和控制面板卸载入口...");
            CopyUninstaller(target);
            CreateShortcut(target);
            CreateStartMenuShortcut(target);
            RegisterUninstallEntry(target);
        }

        public static void Uninstall(string installDir, bool silent)
        {
            string target = Path.GetFullPath(installDir);
            if (!silent)
            {
                var result = MessageBox.Show(Program.ProductName + " 将从以下目录卸载：\r\n" + target + "\r\n\r\n是否继续？",
                                             Program.ProductName + " 卸载程序",
                                             MessageBoxButtons.YesNo,
                                             MessageBoxIcon.Question,
                                             MessageBoxDefaultButton.Button2);
                if (result != DialogResult.Yes) return;
            }

            RemoveShortcut();
            RemoveUninstallEntry();
            ScheduleDirectoryRemoval(target);
            if (!silent)
            {
                MessageBox.Show(Program.ProductName + " 已卸载。", Program.ProductName + " 卸载程序", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
        }

        static void CopyUninstaller(string installDir)
        {
            string current = Assembly.GetExecutingAssembly().Location;
            string uninstaller = Path.Combine(installDir, Program.UninstallerName);
            File.Copy(current, uninstaller, true);
        }

        static void CreateShortcut(string installDir)
        {
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            string shortcut = Path.Combine(desktop, Program.ShortcutName);
            string exe = Path.Combine(installDir, Program.MainExeName);
            CreateShellLink(shortcut, exe, installDir, Program.ProductName, exe, 0);
        }

        static void CreateStartMenuShortcut(string installDir)
        {
            string startMenu = Environment.GetFolderPath(Environment.SpecialFolder.Programs);
            string folder = Path.Combine(startMenu, Program.ProductName);
            Directory.CreateDirectory(folder);
            string shortcut = Path.Combine(folder, Program.ShortcutName);
            string exe = Path.Combine(installDir, Program.MainExeName);
            CreateShellLink(shortcut, exe, installDir, Program.ProductName, exe, 0);
        }

        static void RemoveShortcut()
        {
            try
            {
                string shortcut = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Program.ShortcutName);
                if (File.Exists(shortcut)) File.Delete(shortcut);
                string oldUrl = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "GIS数据质量检查工作台.url");
                if (File.Exists(oldUrl)) File.Delete(oldUrl);
                // Remove Start Menu shortcut and folder
                string startMenu = Environment.GetFolderPath(Environment.SpecialFolder.Programs);
                string folder = Path.Combine(startMenu, Program.ProductName);
                if (Directory.Exists(folder)) Directory.Delete(folder, true);
            }
            catch { }
        }

        static void CreateShellLink(string shortcutPath, string targetPath, string workingDir, string description, string iconPath, int iconIndex)
        {
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null) throw new InvalidOperationException("无法创建快捷方式：WScript.Shell 不可用。");
            object shell = Activator.CreateInstance(shellType);
            object link = null;
            try
            {
                link = shellType.InvokeMember("CreateShortcut", System.Reflection.BindingFlags.InvokeMethod, null, shell, new object[] { shortcutPath });
                Type linkType = link.GetType();
                linkType.InvokeMember("TargetPath", System.Reflection.BindingFlags.SetProperty, null, link, new object[] { targetPath });
                linkType.InvokeMember("WorkingDirectory", System.Reflection.BindingFlags.SetProperty, null, link, new object[] { workingDir });
                linkType.InvokeMember("Description", System.Reflection.BindingFlags.SetProperty, null, link, new object[] { description });
                linkType.InvokeMember("IconLocation", System.Reflection.BindingFlags.SetProperty, null, link, new object[] { iconPath + "," + iconIndex });
                linkType.InvokeMember("Save", System.Reflection.BindingFlags.InvokeMethod, null, link, null);
            }
            finally
            {
                if (link != null && Marshal.IsComObject(link)) Marshal.FinalReleaseComObject(link);
                if (shell != null && Marshal.IsComObject(shell)) Marshal.FinalReleaseComObject(shell);
            }
        }

        static void RegisterUninstallEntry(string installDir)
        {
            string mainExe = Path.Combine(installDir, Program.MainExeName);
            string uninstaller = Path.Combine(installDir, Program.UninstallerName);
            using (var key = Registry.CurrentUser.CreateSubKey(Program.RegistryKeyPath))
            {
                if (key == null) throw new InvalidOperationException("无法创建卸载注册表项。");
                key.SetValue("DisplayName", Program.ProductName, RegistryValueKind.String);
                key.SetValue("DisplayVersion", "0.1.0", RegistryValueKind.String);
                key.SetValue("Publisher", "GISQC", RegistryValueKind.String);
                key.SetValue("InstallLocation", installDir, RegistryValueKind.String);
                key.SetValue("DisplayIcon", mainExe + ",0", RegistryValueKind.String);
                key.SetValue("UninstallString", Quote(uninstaller) + " /uninstall /dir " + Quote(installDir), RegistryValueKind.String);
                key.SetValue("QuietUninstallString", Quote(uninstaller) + " /uninstall /silent /dir " + Quote(installDir), RegistryValueKind.String);
                key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"), RegistryValueKind.String);
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                key.SetValue("EstimatedSize", EstimateDirectorySizeKb(installDir), RegistryValueKind.DWord);
            }
        }

        static void RemoveUninstallEntry()
        {
            try { Registry.CurrentUser.DeleteSubKeyTree(Program.RegistryKeyPath, false); } catch { }
        }

        static int EstimateDirectorySizeKb(string dir)
        {
            try
            {
                long bytes = Directory.GetFiles(dir, "*", SearchOption.AllDirectories).Sum(f => new FileInfo(f).Length);
                long kb = Math.Max(1, bytes / 1024);
                return kb > int.MaxValue ? int.MaxValue : (int)kb;
            }
            catch { return 1; }
        }

        static string Quote(string value)
        {
            return "\"" + value.Replace("\"", "") + "\"";
        }

        static void PreserveLicenseFile(string installDir)
        {
            try
            {
                string oldLicense = Path.Combine(installDir, "license.dat");
                if (!File.Exists(oldLicense)) return;
                string appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                string stateDir = Path.Combine(appData, Program.ProductId);
                Directory.CreateDirectory(stateDir);
                string newLicense = Path.Combine(stateDir, "license.dat");
                if (!File.Exists(newLicense)) File.Copy(oldLicense, newLicense, true);
            }
            catch { }
        }

        static void ScheduleDirectoryRemoval(string installDir)
        {
            try
            {
                foreach (var file in Directory.GetFiles(installDir, "*", SearchOption.AllDirectories)) File.SetAttributes(file, FileAttributes.Normal);
            }
            catch { }

            string tempScript = Path.Combine(Path.GetTempPath(), "GISQCWorkbench_uninstall_" + Guid.NewGuid().ToString("N") + ".cmd");
            string script = "@echo off\r\n" +
                            "chcp 65001 >nul\r\n" +
                            "timeout /t 2 /nobreak >nul\r\n" +
                            "rmdir /s /q \"" + installDir + "\"\r\n" +
                            "del \"%~f0\"\r\n";
            File.WriteAllText(tempScript, script, new UTF8Encoding(false));
            Process.Start(new ProcessStartInfo("cmd.exe", "/c \"" + tempScript + "\"") { CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden });
        }
    }
}
