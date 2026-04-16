$ErrorActionPreference = 'Stop'

$packageName = 'open-dump-viewer'
$version     = '4.0.0'

# Detect architecture
$is64bit = [System.Environment]::Is64BitOperatingSystem
$arch    = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'x64' }

$url_x64   = "https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/download/v${version}/OpenDumpViewer_v${version}_installer_x64.exe"
$url_arm64 = "https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/download/v${version}/OpenDumpViewer_v${version}_installer_arm64.exe"

$checksum_x64   = '92280e089ba8aab25791192c3ecdd42a5de3d22d8a9907f87c25116f6f502091'
$checksum_arm64 = '353c8763b3846a15c2cfbfeba18a706f5a1a6a18e75faa8bd4266434cfdb1508'

$url      = if ($arch -eq 'arm64') { $url_arm64 } else { $url_x64 }
$checksum = if ($arch -eq 'arm64') { $checksum_arm64 } else { $checksum_x64 }

$packageArgs = @{
  packageName    = $packageName
  fileType       = 'exe'
  url64bit       = $url
  checksum64     = $checksum
  checksumType64 = 'sha256'
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
}

Install-ChocolateyPackage @packageArgs