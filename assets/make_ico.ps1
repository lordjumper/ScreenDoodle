# Build a multi-resolution Windows .ico with PNG-embedded entries from a single source PNG.
Add-Type -AssemblyName System.Drawing

$srcPath = Join-Path $PSScriptRoot 'ScreenDoodle.png'
$dstPath = Join-Path $PSScriptRoot 'ScreenDoodle.ico'
$sizes   = 16, 24, 32, 48, 64, 128, 256

$src = [System.Drawing.Image]::FromFile($srcPath)
try {
    $entries = New-Object System.Collections.ArrayList
    foreach ($s in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $g.Clear([System.Drawing.Color]::Transparent)
            $g.DrawImage($src, 0, 0, $s, $s)
        } finally { $g.Dispose() }
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        [void]$entries.Add(@{ Size = $s; Bytes = $ms.ToArray() })
        $ms.Dispose()
    }

    $count = $entries.Count
    $headerSize = 6 + 16 * $count
    $offset = $headerSize

    $bw = New-Object System.IO.BinaryWriter ([System.IO.File]::Open($dstPath, [System.IO.FileMode]::Create))
    try {
        # ICONDIR
        $bw.Write([UInt16]0)   # reserved
        $bw.Write([UInt16]1)   # type=icon
        $bw.Write([UInt16]$count)
        # ICONDIRENTRY[]
        foreach ($e in $entries) {
            $sz = $e.Size
            $bw.Write([byte]($(if ($sz -eq 256) { 0 } else { $sz })))   # width
            $bw.Write([byte]($(if ($sz -eq 256) { 0 } else { $sz })))   # height
            $bw.Write([byte]0)       # color count
            $bw.Write([byte]0)       # reserved
            $bw.Write([UInt16]1)     # planes
            $bw.Write([UInt16]32)    # bpp
            $bw.Write([UInt32]$e.Bytes.Length)
            $bw.Write([UInt32]$offset)
            $offset += $e.Bytes.Length
        }
        # Image data
        foreach ($e in $entries) { $bw.Write($e.Bytes) }
    } finally { $bw.Dispose() }
} finally { $src.Dispose() }

"$($dstPath)  $((Get-Item $dstPath).Length) bytes"
