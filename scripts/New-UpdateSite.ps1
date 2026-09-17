[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$InstallerPath,
    [Parameter(Mandatory)] [string]$OutputDirectory,
    [Parameter(Mandatory)] [string]$BaseUrl,
    [Parameter(Mandatory)] [string]$Version,
    [string]$Notes = "",
    [ValidateSet("windows-x64")] [string]$Platform = "windows-x64"
)

$ErrorActionPreference = "Stop"

$installer = Get-Item -LiteralPath $InstallerPath
if (-not $installer.Extension.Equals(".exe", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The Windows update artifact must be an .exe installer."
}
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "Version must use the numeric major.minor.patch format."
}

$baseUri = [Uri]($BaseUrl.TrimEnd('/') + '/')
if ($baseUri.Scheme -ne "https") { throw "BaseUrl must use HTTPS." }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$target = Join-Path $resolvedOutput $installer.Name
Copy-Item -LiteralPath $installer.FullName -Destination $target -Force

$hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $target).Length
$artifactUrl = [Uri]::new($baseUri, $installer.Name).AbsoluteUri
$manifest = [ordered]@{
    schema = 1
    version = $Version
    published_at = [DateTime]::UtcNow.ToString("o")
    notes = $Notes
    platforms = [ordered]@{
        $Platform = [ordered]@{
            url = $artifactUrl
            sha256 = $hash
            size = $size
        }
    }
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resolvedOutput "updates.json") -Encoding utf8

$safeVersion = [System.Net.WebUtility]::HtmlEncode($Version)
$safeFileName = [System.Net.WebUtility]::HtmlEncode($installer.Name)
$safeNotes = [System.Net.WebUtility]::HtmlEncode($Notes)
$html = @"
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DannyDee Track Manager $safeVersion</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; background: #0b0d13; color: #f4f6fa; }
    body { margin: 0; min-height: 100vh; display: grid; place-items: center; }
    main { width: min(640px, calc(100% - 48px)); background: #131722; border: 1px solid #2b3241;
           border-radius: 18px; padding: 34px; box-shadow: 0 24px 70px #0008; }
    p { color: #aab1c1; line-height: 1.6; } a { display: inline-block; margin-top: 12px; padding: 12px 18px;
        border-radius: 10px; background: #7c4dff; color: white; font-weight: 700; text-decoration: none; }
    code { color: #7de7cd; } small { display: block; margin-top: 22px; color: #778096; }
  </style>
</head>
<body><main>
  <h1>DannyDee Track Manager $safeVersion</h1>
  <p>$safeNotes</p>
  <a href="$safeFileName">Windows-Installer herunterladen</a>
  <small>Der Installer wird in der App vor dem Start per SHA-256 gepr&uuml;ft.</small>
</main></body>
</html>
"@
$html | Set-Content -LiteralPath (Join-Path $resolvedOutput "index.html") -Encoding utf8
New-Item -ItemType File -Force -Path (Join-Path $resolvedOutput ".nojekyll") | Out-Null

Write-Host "Update site created: $resolvedOutput"
Write-Host "Manifest: $($baseUri.AbsoluteUri)updates.json"
Write-Host "SHA256: $hash"
