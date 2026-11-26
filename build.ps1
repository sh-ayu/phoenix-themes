param(
    [switch]$Test,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Dist = Join-Path $Root 'dist'
$Obj = Join-Path $Root 'build'
New-Item -ItemType Directory -Force -Path $Dist, $Obj | Out-Null

function Find-VcVars {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $bat = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $bat) { return $bat }
        }
    }
    $fallback = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat",
                              "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($fallback) { return $fallback }
    throw "vcvars64.bat not found. Install Visual Studio Build Tools (C++ x64)."
}

$vcvars = Find-VcVars
Write-Host "Using MSVC environment: $vcvars"

$mh = Join-Path $Root 'third_party\minhook\src'
$steps = @(
    "call `"$vcvars`" >nul"
    "cl /nologo /c /O2 /MT /W3 /Fo`"$Obj\mh_buffer.obj`" `"$mh\buffer.c`""
    "cl /nologo /c /O2 /MT /W3 /Fo`"$Obj\mh_hook.obj`" `"$mh\hook.c`""
    "cl /nologo /c /O2 /MT /W3 /Fo`"$Obj\mh_trampoline.obj`" `"$mh\trampoline.c`""
    "cl /nologo /c /O2 /MT /W3 /Fo`"$Obj\mh_hde64.obj`" `"$mh\hde\hde64.c`""
    "cl /nologo /std:c++17 /utf-8 /O2 /MT /W4 /WX /LD /EHsc /Fo`"$Obj\phoenix_themes.obj`" `"$Root\src\phoenix_themes.cpp`" `"$Obj\mh_buffer.obj`" `"$Obj\mh_hook.obj`" `"$Obj\mh_trampoline.obj`" `"$Obj\mh_hde64.obj`" /Fe:`"$Dist\phoenix-themes.dll`" user32.lib dwrite.lib dwmapi.lib ole32.lib uuid.lib comdlg32.lib shell32.lib windowscodecs.lib"
    "cl /nologo /std:c++17 /utf-8 /O2 /MT /W4 /WX /EHsc /Fo`"$Obj\launcher.obj`" `"$Root\src\launcher.cpp`" /Fe:`"$Dist\phoenix-themes.exe`" /link /SUBSYSTEM:WINDOWS user32.lib kernel32.lib shell32.lib ole32.lib uuid.lib winhttp.lib"
)
$cmd = $steps -join ' && '

cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

New-Item -ItemType Directory -Force -Path "$Dist\adapters\sunbird", "$Dist\themes" | Out-Null
Copy-Item "$Root\adapters\sunbird\phoenix-themes.js" "$Dist\adapters\sunbird\phoenix-themes.js" -Force
Copy-Item "$Root\themes\*.theme.css" "$Dist\themes\" -Force
Remove-Item "$Dist\phoenix-themes.lib", "$Dist\phoenix-themes.exp" -ErrorAction SilentlyContinue

Write-Host "`nBuilt artifacts in $Dist`:"
Get-ChildItem $Dist | Select-Object Name, Length | Format-Table -AutoSize

if ($Test) {
    Write-Host "Checking Sunbird adapter JS syntax..."
    node --check "$Root\adapters\sunbird\phoenix-themes.js"
    if ($LASTEXITCODE -ne 0) { throw "JS syntax check failed" }

    Write-Host "Building and running unit tests..."
    $testCmd = "call `"$vcvars`" >nul && cl /nologo /std:c++17 /utf-8 /O2 /MT /W4 /WX /EHsc /Fo`"$Obj\test_themes.obj`" `"$Root\tests\test_themes.cpp`" /Fe:`"$Obj\test_themes.exe`" dwrite.lib ole32.lib"
    cmd /c $testCmd
    if ($LASTEXITCODE -ne 0) { throw "Test build failed" }

    foreach ($mode in @('cjk_fallback', 'all', 'custom_file')) {
        & "$Obj\test_themes.exe" "$Dist\phoenix-themes.dll" $mode
        if ($LASTEXITCODE -ne 0) { throw "Test mode '$mode' failed" }
    }
    Write-Host "All tests passed!"
}

if ($Install) {
    $baseDir    = Join-Path $env:APPDATA 'phoenix-themes'
    $themesDir  = Join-Path $baseDir 'themes'
    $pluginsDir = Join-Path $env:LOCALAPPDATA 'DiscordPhoenix\sunbird\plugins'
    New-Item -ItemType Directory -Force -Path $baseDir, $themesDir, $pluginsDir | Out-Null

    foreach ($f in @('phoenix-themes.dll', 'phoenix-themes.exe')) {
        $dst = Join-Path $baseDir $f
        $old = "$dst.old"
        Remove-Item $old -Force -ErrorAction SilentlyContinue
        if (Test-Path $dst) {
            try { Remove-Item $dst -Force -ErrorAction Stop }
            catch { Move-Item $dst $old -Force }
        }
        Copy-Item (Join-Path $Dist $f) $dst -Force
        Remove-Item $old -Force -ErrorAction SilentlyContinue
    }

    Get-ChildItem "$Dist\themes\*.theme.css" | ForEach-Object {
        $dstTheme = Join-Path $themesDir $_.Name
        if (-not (Test-Path $dstTheme)) {
            Copy-Item $_.FullName $dstTheme -Force
        }
    }

    # Install thin Sunbird UI adapter
    Copy-Item "$Dist\adapters\sunbird\phoenix-themes.js" (Join-Path $pluginsDir 'phoenix-themes.js') -Force

    # Update Desktop & Start Menu Sunbird shortcuts to launch via phoenix-themes.exe
    $w = New-Object -ComObject WScript.Shell
    $iconPath = (Join-Path $env:LOCALAPPDATA 'DiscordPhoenix\app\Discord.exe') + ',0'
    foreach ($dir in @([Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'))) {
        $lnk = Join-Path $dir 'Sunbird.lnk'
        $s = $w.CreateShortcut($lnk)
        $s.TargetPath = Join-Path $baseDir 'phoenix-themes.exe'
        $s.IconLocation = $iconPath
        $s.Description = 'Discord Phoenix with Phoenix Themes'
        $s.Save()
    }

    Write-Host "Installed phoenix-themes into $baseDir (themes in $themesDir) and Sunbird adapter into $pluginsDir."
}
