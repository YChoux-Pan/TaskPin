param(
    [string]$Image = '',
    [int]$X = 100,
    [int]$Y = 100,
    [int]$W = 720,
    [int]$H = 220
)

if (-not $Image) { $Image = Join-Path $PSScriptRoot 'fixtures\ocr-probe.png' }

# Test fixture: a borderless, always-on-top window that shows one PNG at a known rectangle.
# Used by test-snip.ps1 so the screenshot / OCR checks have deterministic content and geometry.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Dpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    public static void Aware() { try { SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch {} }
}
'@
[Dpi]::Aware()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'TaskPinTestImage'
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$form.Location = New-Object System.Drawing.Point $X, $Y
$form.Size = New-Object System.Drawing.Size $W, $H
$form.AutoScaleMode = [System.Windows.Forms.AutoScaleMode]::None
$form.TopMost = $true
$form.BackColor = [System.Drawing.Color]::White

$picture = New-Object System.Windows.Forms.PictureBox
$picture.Dock = [System.Windows.Forms.DockStyle]::Fill
$picture.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::Zoom
$picture.Image = [System.Drawing.Image]::FromFile($Image)
$form.Controls.Add($picture)

$form.Show()
$form.Activate()
[System.Windows.Forms.Application]::Run($form)
