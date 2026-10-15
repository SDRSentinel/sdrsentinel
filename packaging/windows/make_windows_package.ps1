param(
    [string]$BuildDir = "./build",
    [string]$RootDir = "./root"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$BuildDir = (Resolve-Path $BuildDir).Path
$RootDir = (Resolve-Path $RootDir).Path
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("sdrsentinel-package-" + [guid]::NewGuid())
$installDir = Join-Path $workDir "install"
$packageDir = Join-Path $workDir "sdrpp_windows_x64"

try {
    # The install rules identify exactly which modules were built, including
    # targets whose DLL name differs from their source directory name.
    & cmake --install $BuildDir --config Release --prefix $installDir
    if ($LASTEXITCODE -ne 0) { throw "CMake installation failed ($LASTEXITCODE)" }
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    Copy-Item -Path (Join-Path $RootDir "*") -Destination $packageDir -Recurse -Force
    $moduleDir = Join-Path $packageDir "modules"
    New-Item -ItemType Directory -Path $moduleDir -Force | Out-Null
    Copy-Item (Join-Path $installDir "bin/sdrpp.exe") $packageDir
    Copy-Item (Join-Path $installDir "lib/sdrpp_core.dll") $packageDir
    $modules = @(Get-ChildItem (Join-Path $installDir "lib/sdrpp/plugins") -Filter "*.dll" -File)
    if ($modules.Count -eq 0) { throw "No built module DLLs were installed" }
    $modules | Copy-Item -Destination $moduleDir

    # dumpbin is installed by Visual Studio, but is not necessarily on PATH.
    $dumpbinCommand = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($dumpbinCommand) {
        $dumpbin = $dumpbinCommand.Source
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
        $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or -not $visualStudio) { throw "Visual Studio C++ tools were not found" }
        $tools = Get-ChildItem (Join-Path $visualStudio "VC/Tools/MSVC") -Directory |
            Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        $dumpbin = Join-Path $tools.FullName "bin/Hostx64/x64/dumpbin.exe"
    }
    if (-not (Test-Path $dumpbin -PathType Leaf)) { throw "dumpbin.exe was not found" }

    # Prefer Release output (including app-local vcpkg DLLs), then the vcpkg and
    # Pothos installations used by this build. Optional modules need no entries.
    $runtimeFiles = @{}
    $builtDlls = Get-ChildItem $BuildDir -Filter "*.dll" -Recurse -File |
        Where-Object { $_.FullName -match '[\\/]Release[\\/]' -or $_.DirectoryName -eq $BuildDir }
    foreach ($dll in $builtDlls) {
        if (-not $runtimeFiles.ContainsKey($dll.Name)) { $runtimeFiles[$dll.Name] = $dll.FullName }
    }
    $runtimeDirs = @(
        (Join-Path $BuildDir "vcpkg_installed/x64-windows/bin"),
        (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) "vcpkg_installed/x64-windows/bin"),
        "C:/Program Files/PothosSDR/bin",
        "C:/Program Files (x86)/RtAudio/bin",
        "C:/Program Files/RigExpert/Fobos/bin",
        "C:/Program Files/RFNM/bin",
        "C:/Program Files/hydrasdr-host/bin",
        "C:/Program Files/SDRplay/API/x64",
        "C:/Program Files/codec2/lib"
    )
    if ($env:VCPKG_ROOT) { $runtimeDirs += Join-Path $env:VCPKG_ROOT "installed/x64-windows/bin" }
    if ($env:VCPKG_INSTALLED_DIR) { $runtimeDirs += Join-Path $env:VCPKG_INSTALLED_DIR "x64-windows/bin" }
    foreach ($directory in $runtimeDirs) {
        if (-not (Test-Path $directory -PathType Container)) { continue }
        foreach ($dll in Get-ChildItem $directory -Filter "*.dll" -File) {
            if (-not $runtimeFiles.ContainsKey($dll.Name)) { $runtimeFiles[$dll.Name] = $dll.FullName }
        }
    }

    $pending = [System.Collections.Generic.Queue[string]]::new()
    Get-ChildItem $packageDir -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' } |
        ForEach-Object { $pending.Enqueue($_.FullName) }
    $checked = @{}
    while ($pending.Count -gt 0) {
        $binary = $pending.Dequeue()
        if ($checked.ContainsKey($binary)) { continue }
        $checked[$binary] = $true
        $imports = & $dumpbin /nologo /dependents $binary
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect dependencies of $binary" }
        foreach ($line in $imports) {
            if ($line -notmatch '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { continue }
            $name = $Matches[1]
            # Windows API sets and system DLLs are supplied by the OS. Resolve
            # shipped libraries first so the VC++ runtime is bundled as well.
            if ($name -match '^(api-ms-win-|ext-ms-win-)') { continue }
            $destination = Join-Path $packageDir $name
            if (Test-Path $destination -PathType Leaf) {
                $pending.Enqueue($destination)
            } elseif ($runtimeFiles.ContainsKey($name)) {
                Copy-Item $runtimeFiles[$name] $destination
                $pending.Enqueue($destination)
            } elseif ($name -match '^(msvcp|vcruntime|concrt|vcomp)[0-9].*\.dll$' -or
                -not (Test-Path (Join-Path $env:SystemRoot "System32/$name") -PathType Leaf)) {
                throw "Missing runtime dependency '$name' required by '$binary'"
            }
        }
    }

    $archive = Join-Path (Get-Location) "sdrpp_windows_x64.zip"
    Compress-Archive -Path $packageDir -DestinationPath $archive -Force
    Copy-Item $archive (Join-Path (Get-Location) "sdrsentinel_windows_x64.zip") -Force
} finally {
    if (Test-Path $workDir) { Remove-Item $workDir -Recurse -Force }
}
