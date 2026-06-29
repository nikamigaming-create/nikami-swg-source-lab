param(
    [Parameter(Mandatory = $true)]
    [string]$ReferenceImage,

    [Parameter(Mandatory = $true)]
    [string]$CandidateImage,

    [string]$OutputDirectory = "",

    [int]$HeatScale = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class RenderImageComparisonMetrics
{
    public double SumAbsR;
    public double SumAbsG;
    public double SumAbsB;
    public double SumSignedR;
    public double SumSignedG;
    public double SumSignedB;
    public int MaxAbs;
    public int ChangedPixels;
    public int ThresholdPixels;
}

public sealed class RenderImageAnalysisMetrics
{
    public int Width;
    public int Height;
    public int PixelCount;
    public double SumR;
    public double SumG;
    public double SumB;
    public double SumLuma;
    public int BlueDominantPixels;
    public int CyanDominantPixels;
    public int WarmTerrainPixels;
    public int DarkPixels;
    public int BrightPixels;
}

public static class RenderImageComparisonFast
{
    private static byte ClampByte(int value)
    {
        if (value < 0)
            return 0;
        if (value > 255)
            return 255;
        return (byte)value;
    }

    private static Bitmap ConvertToArgb32(Image source, int width, int height)
    {
        Bitmap result = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(result))
        {
            graphics.DrawImage(source, new Rectangle(0, 0, width, height));
        }
        return result;
    }

    public static RenderImageComparisonMetrics Compare(Image referenceImage, Image candidateImage, Bitmap diffImage, Bitmap signedImage, int heatScale, int threshold)
    {
        int width = diffImage.Width;
        int height = diffImage.Height;
        RenderImageComparisonMetrics metrics = new RenderImageComparisonMetrics();

        using (Bitmap reference = ConvertToArgb32(referenceImage, width, height))
        using (Bitmap candidate = ConvertToArgb32(candidateImage, width, height))
        {
            Rectangle rect = new Rectangle(0, 0, width, height);
            BitmapData referenceData = reference.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            BitmapData candidateData = candidate.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            BitmapData diffData = diffImage.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            BitmapData signedData = signedImage.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = referenceData.Stride * height;
                byte[] referenceBytes = new byte[byteCount];
                byte[] candidateBytes = new byte[byteCount];
                byte[] diffBytes = new byte[byteCount];
                byte[] signedBytes = new byte[byteCount];

                Marshal.Copy(referenceData.Scan0, referenceBytes, 0, byteCount);
                Marshal.Copy(candidateData.Scan0, candidateBytes, 0, byteCount);

                for (int y = 0; y < height; ++y)
                {
                    int row = y * referenceData.Stride;
                    for (int x = 0; x < width; ++x)
                    {
                        int offset = row + x * 4;
                        int db = candidateBytes[offset + 0] - referenceBytes[offset + 0];
                        int dg = candidateBytes[offset + 1] - referenceBytes[offset + 1];
                        int dr = candidateBytes[offset + 2] - referenceBytes[offset + 2];

                        int ab = Math.Abs(db);
                        int ag = Math.Abs(dg);
                        int ar = Math.Abs(dr);
                        int localMax = Math.Max(ar, Math.Max(ag, ab));

                        metrics.SumAbsR += ar;
                        metrics.SumAbsG += ag;
                        metrics.SumAbsB += ab;
                        metrics.SumSignedR += dr;
                        metrics.SumSignedG += dg;
                        metrics.SumSignedB += db;
                        if (localMax > metrics.MaxAbs)
                            metrics.MaxAbs = localMax;
                        if (localMax > 0)
                            ++metrics.ChangedPixels;
                        if (localMax >= threshold)
                            ++metrics.ThresholdPixels;

                        diffBytes[offset + 0] = ClampByte(ab * heatScale);
                        diffBytes[offset + 1] = ClampByte(ag * heatScale);
                        diffBytes[offset + 2] = ClampByte(ar * heatScale);
                        diffBytes[offset + 3] = 255;

                        signedBytes[offset + 0] = ClampByte(128 + db);
                        signedBytes[offset + 1] = ClampByte(128 + dg);
                        signedBytes[offset + 2] = ClampByte(128 + dr);
                        signedBytes[offset + 3] = 255;
                    }
                }

                Marshal.Copy(diffBytes, 0, diffData.Scan0, byteCount);
                Marshal.Copy(signedBytes, 0, signedData.Scan0, byteCount);
            }
            finally
            {
                reference.UnlockBits(referenceData);
                candidate.UnlockBits(candidateData);
                diffImage.UnlockBits(diffData);
                signedImage.UnlockBits(signedData);
            }
        }

        return metrics;
    }

    public static RenderImageAnalysisMetrics Analyze(Image sourceImage, int width, int height)
    {
        RenderImageAnalysisMetrics metrics = new RenderImageAnalysisMetrics();
        metrics.Width = width;
        metrics.Height = height;
        metrics.PixelCount = width * height;

        using (Bitmap source = ConvertToArgb32(sourceImage, width, height))
        {
            Rectangle rect = new Rectangle(0, 0, width, height);
            BitmapData data = source.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = data.Stride * height;
                byte[] bytes = new byte[byteCount];
                Marshal.Copy(data.Scan0, bytes, 0, byteCount);

                for (int y = 0; y < height; ++y)
                {
                    int row = y * data.Stride;
                    for (int x = 0; x < width; ++x)
                    {
                        int offset = row + x * 4;
                        int b = bytes[offset + 0];
                        int g = bytes[offset + 1];
                        int r = bytes[offset + 2];
                        double luma = (r * 0.2126) + (g * 0.7152) + (b * 0.0722);

                        metrics.SumR += r;
                        metrics.SumG += g;
                        metrics.SumB += b;
                        metrics.SumLuma += luma;

                        if (b > r + 30 && b > g + 10 && b > 45)
                            ++metrics.BlueDominantPixels;
                        if (g > r + 20 && b > r + 20 && g > 50 && b > 50 && Math.Abs(g - b) < 80)
                            ++metrics.CyanDominantPixels;
                        if (r > 60 && r > b + 15 && g > b - 5)
                            ++metrics.WarmTerrainPixels;
                        if (luma < 45.0)
                            ++metrics.DarkPixels;
                        if (luma > 190.0)
                            ++metrics.BrightPixels;
                    }
                }
            }
            finally
            {
                source.UnlockBits(data);
            }
        }

        return metrics;
    }
}
'@

function Resolve-AbsolutePath([string]$Path) {
    return [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
}

function ClampByte([int]$Value) {
    if ($Value -lt 0) { return 0 }
    if ($Value -gt 255) { return 255 }
    return $Value
}

function Draw-ImagePanel {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Image]$Image,
        [string]$Label,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height
    )

    $labelHeight = 28
    $font = [System.Drawing.Font]::new("Arial", 12, [System.Drawing.FontStyle]::Bold)
    $labelBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(235, 235, 235))
    $textBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::Black)
    try {
        $Graphics.FillRectangle($labelBrush, $X, $Y, $Width, $labelHeight)
        $Graphics.DrawString($Label, $font, $textBrush, [System.Drawing.PointF]::new($X + 8, $Y + 6))
        $Graphics.DrawImage($Image, [System.Drawing.Rectangle]::new($X, $Y + $labelHeight, $Width, $Height))
    }
    finally {
        $textBrush.Dispose()
        $labelBrush.Dispose()
        $font.Dispose()
    }
}

function Convert-ImageAnalysisSummary {
    param([RenderImageAnalysisMetrics]$Metrics)

    $pixelCount = [double]$Metrics.PixelCount
    $meanR = $Metrics.SumR / $pixelCount
    $meanG = $Metrics.SumG / $pixelCount
    $meanB = $Metrics.SumB / $pixelCount
    $blueDominantPercent = ($Metrics.BlueDominantPixels / $pixelCount) * 100.0
    $cyanDominantPercent = ($Metrics.CyanDominantPixels / $pixelCount) * 100.0
    $warmTerrainPercent = ($Metrics.WarmTerrainPixels / $pixelCount) * 100.0
    $darkPercent = ($Metrics.DarkPixels / $pixelCount) * 100.0

    $loadingOverlayLikely = (($cyanDominantPercent -ge 30.0 -or $blueDominantPercent -ge 20.0) -and
        $warmTerrainPercent -lt 25.0 -and
        (($meanB - $meanR) -gt 15.0 -or $darkPercent -ge 45.0))

    [ordered]@{
        width = $Metrics.Width
        height = $Metrics.Height
        pixelCount = $Metrics.PixelCount
        meanR = [Math]::Round($meanR, 4)
        meanG = [Math]::Round($meanG, 4)
        meanB = [Math]::Round($meanB, 4)
        meanLuma = [Math]::Round($Metrics.SumLuma / $pixelCount, 4)
        blueDominantPercent = [Math]::Round($blueDominantPercent, 4)
        cyanDominantPercent = [Math]::Round($cyanDominantPercent, 4)
        warmTerrainPercent = [Math]::Round($warmTerrainPercent, 4)
        darkPercent = [Math]::Round($darkPercent, 4)
        brightPercent = [Math]::Round(($Metrics.BrightPixels / $pixelCount) * 100.0, 4)
        loadingOverlayLikely = $loadingOverlayLikely
    }
}

function Compare-ImageStateSummaries {
    param(
        [hashtable]$ReferenceStats,
        [hashtable]$CandidateStats
    )

    $reasons = @()

    if ([bool]$ReferenceStats.loadingOverlayLikely -ne [bool]$CandidateStats.loadingOverlayLikely) {
        $reasons += "loadingOverlayLikely differs"
    }

    $warmDelta = [Math]::Abs([double]$ReferenceStats.warmTerrainPercent - [double]$CandidateStats.warmTerrainPercent)
    $cyanDelta = [Math]::Abs([double]$ReferenceStats.cyanDominantPercent - [double]$CandidateStats.cyanDominantPercent)
    $blueDelta = [Math]::Abs([double]$ReferenceStats.blueDominantPercent - [double]$CandidateStats.blueDominantPercent)
    $lumaDelta = [Math]::Abs([double]$ReferenceStats.meanLuma - [double]$CandidateStats.meanLuma)

    if ($warmDelta -ge 35.0 -and ($cyanDelta -ge 20.0 -or $blueDelta -ge 15.0)) {
        $reasons += "large warm/cyan-blue scene-composition delta"
    }

    if ($lumaDelta -ge 60.0 -and ($cyanDelta -ge 20.0 -or $blueDelta -ge 15.0)) {
        $reasons += "large luma and blue/cyan delta"
    }

    [ordered]@{
        probableStateMismatch = ($reasons.Count -gt 0)
        reasons = $reasons
        warmTerrainPercentDelta = [Math]::Round($warmDelta, 4)
        cyanDominantPercentDelta = [Math]::Round($cyanDelta, 4)
        blueDominantPercentDelta = [Math]::Round($blueDelta, 4)
        meanLumaDelta = [Math]::Round($lumaDelta, 4)
    }
}

$referencePath = Resolve-AbsolutePath $ReferenceImage
$candidatePath = Resolve-AbsolutePath $CandidateImage

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path (Split-Path -Parent $candidatePath) "pixel-diff"
}
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$ref = [System.Drawing.Bitmap]::new($referencePath)
$cand = [System.Drawing.Bitmap]::new($candidatePath)
$diff = $null
$signed = $null
$sideBySide = $null
$contactSheet = $null
try {
    $width = [Math]::Min($ref.Width, $cand.Width)
    $height = [Math]::Min($ref.Height, $cand.Height)
    if ($width -le 0 -or $height -le 0) {
        throw "Images have no overlapping pixel area."
    }

    $diff = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $signed = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    $threshold = 10
    $metrics = [RenderImageComparisonFast]::Compare($ref, $cand, $diff, $signed, $HeatScale, $threshold)
    $referenceStats = Convert-ImageAnalysisSummary ([RenderImageComparisonFast]::Analyze($ref, $width, $height))
    $candidateStats = Convert-ImageAnalysisSummary ([RenderImageComparisonFast]::Analyze($cand, $width, $height))
    $stateComparison = Compare-ImageStateSummaries -ReferenceStats $referenceStats -CandidateStats $candidateStats
    $comparisonValidity = if ($stateComparison.probableStateMismatch) { "RejectedStateMismatch" } else { "ComparableFrameState" }

    $pixelCount = [double]($width * $height)
    $diffPath = Join-Path $outputPath "abs-diff.png"
    $signedPath = Join-Path $outputPath "signed-diff.png"
    $sideBySidePath = Join-Path $outputPath "side-by-side.png"
    $contactSheetPath = Join-Path $outputPath "contact-sheet.png"
    $summaryPath = Join-Path $outputPath "summary.json"
    $diff.Save($diffPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $signed.Save($signedPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $labelHeight = 28
    $sideBySide = [System.Drawing.Bitmap]::new($width * 2, $height + $labelHeight, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $sideGraphics = [System.Drawing.Graphics]::FromImage($sideBySide)
    try {
        $sideGraphics.Clear([System.Drawing.Color]::Black)
        Draw-ImagePanel -Graphics $sideGraphics -Image $ref -Label "D3D9 reference" -X 0 -Y 0 -Width $width -Height $height
        Draw-ImagePanel -Graphics $sideGraphics -Image $cand -Label "D3D11 candidate" -X $width -Y 0 -Width $width -Height $height
        $sideBySide.Save($sideBySidePath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $sideGraphics.Dispose()
    }

    $contactSheet = [System.Drawing.Bitmap]::new($width * 2, ($height + $labelHeight) * 2, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $contactGraphics = [System.Drawing.Graphics]::FromImage($contactSheet)
    try {
        $contactGraphics.Clear([System.Drawing.Color]::Black)
        Draw-ImagePanel -Graphics $contactGraphics -Image $ref -Label "D3D9 reference" -X 0 -Y 0 -Width $width -Height $height
        Draw-ImagePanel -Graphics $contactGraphics -Image $cand -Label "D3D11 candidate" -X $width -Y 0 -Width $width -Height $height
        Draw-ImagePanel -Graphics $contactGraphics -Image $diff -Label "absolute diff x$HeatScale" -X 0 -Y ($height + $labelHeight) -Width $width -Height $height
        Draw-ImagePanel -Graphics $contactGraphics -Image $signed -Label "signed diff, gray means equal" -X $width -Y ($height + $labelHeight) -Width $width -Height $height
        $contactSheet.Save($contactSheetPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $contactGraphics.Dispose()
    }

    $summary = [ordered]@{
        referenceImage = $referencePath
        candidateImage = $candidatePath
        referenceSourceWidth = $ref.Width
        referenceSourceHeight = $ref.Height
        candidateSourceWidth = $cand.Width
        candidateSourceHeight = $cand.Height
        width = $width
        height = $height
        pixelCount = [int]$pixelCount
        changedPixels = $metrics.ChangedPixels
        changedPercent = [Math]::Round(($metrics.ChangedPixels / $pixelCount) * 100.0, 4)
        threshold = $threshold
        thresholdPixels = $metrics.ThresholdPixels
        thresholdPercent = [Math]::Round(($metrics.ThresholdPixels / $pixelCount) * 100.0, 4)
        meanAbsR = [Math]::Round($metrics.SumAbsR / $pixelCount, 4)
        meanAbsG = [Math]::Round($metrics.SumAbsG / $pixelCount, 4)
        meanAbsB = [Math]::Round($metrics.SumAbsB / $pixelCount, 4)
        meanAbsRgb = [Math]::Round(($metrics.SumAbsR + $metrics.SumAbsG + $metrics.SumAbsB) / ($pixelCount * 3.0), 4)
        meanSignedR = [Math]::Round($metrics.SumSignedR / $pixelCount, 4)
        meanSignedG = [Math]::Round($metrics.SumSignedG / $pixelCount, 4)
        meanSignedB = [Math]::Round($metrics.SumSignedB / $pixelCount, 4)
        maxAbsChannel = $metrics.MaxAbs
        comparisonValidity = $comparisonValidity
        stateComparison = $stateComparison
        referenceImageStats = $referenceStats
        candidateImageStats = $candidateStats
        absDiffImage = $diffPath
        signedDiffImage = $signedPath
        sideBySideImage = $sideBySidePath
        contactSheetImage = $contactSheetPath
    }

    $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $summary | Format-List
}
finally {
    if ($contactSheet) { $contactSheet.Dispose() }
    if ($sideBySide) { $sideBySide.Dispose() }
    if ($diff) { $diff.Dispose() }
    if ($signed) { $signed.Dispose() }
    $ref.Dispose()
    $cand.Dispose()
}
