using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace GISQCLicenseKeygen
{
    static class Program
    {
        public static Icon LoadAppIcon()
        {
            try
            {
                using (var input = typeof(Program).Assembly.GetManifestResourceStream("app.ico"))
                {
                    if (input != null) return new Icon(input);
                }
            }
            catch { }
            return SystemIcons.Application;
        }

        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new KeygenForm());
            return 0;
        }
    }

    sealed class KeygenForm : Form
    {
        readonly TextBox machineBox = new TextBox();
        readonly DateTimePicker expirePicker = new DateTimePicker();
        readonly NumericUpDown runsBox = new NumericUpDown();
        readonly TextBox codeBox = new TextBox();
        readonly Button generateButton = new Button();
        readonly Button copyButton = new Button();
        readonly Button saveButton = new Button();
        readonly Button clearButton = new Button();

        public KeygenForm()
        {
            Text = "GIS 质检软件注册机";
            Icon = Program.LoadAppIcon();
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(720, 560);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            Font = new Font("Microsoft YaHei UI", 9F);

            var header = new Panel { Left = 0, Top = 0, Width = 720, Height = 72, BackColor = Color.FromArgb(19, 132, 202) };
            header.Controls.Add(new PictureBox { Left = 22, Top = 12, Width = 48, Height = 48, SizeMode = PictureBoxSizeMode.StretchImage, Image = Program.LoadAppIcon().ToBitmap() });
            header.Controls.Add(new Label { Left = 84, Top = 16, Width = 600, Height = 32, Text = "GIS 数据质量检查工作台 注册机", ForeColor = Color.White, Font = new Font("Microsoft YaHei UI", 15F, FontStyle.Bold) });
            header.Controls.Add(new Label { Left = 86, Top = 48, Width = 600, Height = 18, Text = "输入客户机器码，选择有效期和允许运行次数，生成注册码/授权内容", ForeColor = Color.WhiteSmoke });
            Controls.Add(header);

            AddLabel("客户机器码：", 28, 100);
            machineBox.Left = 150; machineBox.Top = 96; machineBox.Width = 520; machineBox.Height = 28; machineBox.CharacterCasing = CharacterCasing.Upper; Controls.Add(machineBox);

            AddLabel("有效期至：", 28, 144);
            expirePicker.Left = 150; expirePicker.Top = 140; expirePicker.Width = 200; expirePicker.Format = DateTimePickerFormat.Custom; expirePicker.CustomFormat = "yyyy-MM-dd"; expirePicker.Value = DateTime.Today.AddYears(1); Controls.Add(expirePicker);
            var oneYear = new Button { Left = 365, Top = 138, Width = 72, Height = 30, Text = "1年" }; oneYear.Click += (s, e) => expirePicker.Value = DateTime.Today.AddYears(1); Controls.Add(oneYear);
            var threeYear = new Button { Left = 443, Top = 138, Width = 72, Height = 30, Text = "3年" }; threeYear.Click += (s, e) => expirePicker.Value = DateTime.Today.AddYears(3); Controls.Add(threeYear);
            var tenYear = new Button { Left = 521, Top = 138, Width = 72, Height = 30, Text = "10年" }; tenYear.Click += (s, e) => expirePicker.Value = DateTime.Today.AddYears(10); Controls.Add(tenYear);
            var forever = new Button { Left = 599, Top = 138, Width = 72, Height = 30, Text = "长期" }; forever.Click += (s, e) => expirePicker.Value = new DateTime(2099, 12, 31); Controls.Add(forever);

            AddLabel("允许运行次数：", 28, 188);
            runsBox.Left = 150; runsBox.Top = 184; runsBox.Width = 200; runsBox.Minimum = 0; runsBox.Maximum = 999999999; runsBox.Value = 999999; Controls.Add(runsBox);
            Controls.Add(new Label { Left = 365, Top = 188, Width = 280, Height = 24, Text = "0 表示不限次数；建议正式授权填 999999" });

            generateButton.Left = 150; generateButton.Top = 228; generateButton.Width = 120; generateButton.Height = 34; generateButton.Text = "生成注册码"; generateButton.Click += Generate; Controls.Add(generateButton);
            copyButton.Left = 280; copyButton.Top = 228; copyButton.Width = 100; copyButton.Height = 34; copyButton.Text = "复制"; copyButton.Click += (s, e) => { if (!string.IsNullOrWhiteSpace(codeBox.Text)) Clipboard.SetText(codeBox.Text); }; Controls.Add(copyButton);
            saveButton.Left = 390; saveButton.Top = 228; saveButton.Width = 130; saveButton.Height = 34; saveButton.Text = "保存 license.dat"; saveButton.Click += SaveLicense; Controls.Add(saveButton);
            clearButton.Left = 530; clearButton.Top = 228; clearButton.Width = 100; clearButton.Height = 34; clearButton.Text = "清空"; clearButton.Click += (s, e) => { machineBox.Clear(); codeBox.Clear(); }; Controls.Add(clearButton);

            AddLabel("注册码 / 授权内容：", 28, 286);
            codeBox.Left = 28; codeBox.Top = 316; codeBox.Width = 664; codeBox.Height = 190; codeBox.Multiline = true; codeBox.ScrollBars = ScrollBars.Vertical; codeBox.Font = new Font("Consolas", 10F); Controls.Add(codeBox);
            Controls.Add(new Label { Left = 28, Top = 518, Width = 664, Height = 34, Text = "用法：把上方内容保存为 license.dat 后发给客户，或让客户把内容复制到安装目录的 license.dat。" });
        }

        void AddLabel(string text, int left, int top)
        {
            Controls.Add(new Label { Left = left, Top = top, Width = 116, Height = 24, Text = text, TextAlign = ContentAlignment.MiddleRight });
        }

        void Generate(object sender, EventArgs e)
        {
            string machine = machineBox.Text.Trim().ToUpperInvariant();
            if (string.IsNullOrWhiteSpace(machine))
            {
                MessageBox.Show(this, "请输入客户提供的机器码。", Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                machineBox.Focus();
                return;
            }
            string expire = expirePicker.Value.ToString("yyyy-MM-dd");
            int maxRuns = Decimal.ToInt32(runsBox.Value);
            string issuedAt = DateTime.Today.ToString("yyyy-MM-dd");
            string sig = SignatureFor("GISQCWorkbench", machine, expire, maxRuns, issuedAt);
            var sb = new StringBuilder();
            sb.AppendLine("product=GISQCWorkbench");
            sb.AppendLine("machineCode=" + machine);
            sb.AppendLine("expireDate=" + expire);
            sb.AppendLine("maxRuns=" + maxRuns);
            sb.AppendLine("issuedAt=" + issuedAt);
            sb.AppendLine("signature=" + sig);
            codeBox.Text = sb.ToString();
        }

        void SaveLicense(object sender, EventArgs e)
        {
            if (string.IsNullOrWhiteSpace(codeBox.Text)) Generate(sender, e);
            if (string.IsNullOrWhiteSpace(codeBox.Text)) return;
            using (var dlg = new SaveFileDialog { Title = "保存授权文件", Filter = "授权文件 license.dat|license.dat|文本文件|*.txt|所有文件|*.*", FileName = "license.dat" })
            {
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    File.WriteAllText(dlg.FileName, codeBox.Text, Encoding.UTF8);
                    MessageBox.Show(this, "已保存：\r\n" + dlg.FileName, Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            }
        }

        static string SignatureFor(string product, string machineCode, string expireDate, int maxRuns, string issuedAt)
        {
            const string secret = "GISQC-PRIVATE-KEY-2026-05-NOT-FOR-CLIENT-CHANGE";
            string payload = product + "|" + machineCode + "|" + expireDate + "|" + maxRuns + "|" + issuedAt + "|" + secret;
            ulong h1 = Fnv1a64(payload);
            ulong h2 = Fnv1a64(secret + payload + machineCode);
            return UpperHex(h1) + UpperHex(h2);
        }

        static ulong Fnv1a64(string text)
        {
            ulong hash = 14695981039346656037UL;
            foreach (byte b in Encoding.UTF8.GetBytes(text))
            {
                hash ^= b;
                hash *= 1099511628211UL;
            }
            return hash;
        }

        static string UpperHex(ulong value)
        {
            return value.ToString("X16");
        }
    }
}
