$ErrorActionPreference = 'Stop'

$packageName = 'open-dump-viewer'

# Find uninstall entry in registry
$uninstallKey = Get-UninstallRegistryKey -SoftwareName 'Open DUMP Viewer for Oracle database*'

if ($uninstallKey) {
  $uninstallString = $uninstallKey.UninstallString
  $silentArgs      = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART'

  Uninstall-ChocolateyPackage -PackageName $packageName `
    -FileType 'exe' `
    -SilentArgs $silentArgs `
    -File $uninstallString
} else {
  Write-Warning "$packageName is not found in the registry. It may have already been uninstalled."
}