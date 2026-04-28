[CmdletBinding()]
param(
  [ValidateSet("x64", "x84", "arm64")]
  [string]$Target = "x64",

  [switch]$SkipRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$OutDir = Join-Path $RepoRoot "ci\out\$Target"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Set-Location $RepoRoot

function Get-VsDevCmd {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found at $vswhere"
  }

  $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
    throw "Unable to locate a Visual Studio installation with MSVC tools"
  }

  $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
  if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat was not found at $vsDevCmd"
  }

  return $vsDevCmd
}

function Invoke-VsDevCmd {
  param(
    [Parameter(Mandatory = $true)][string]$Arch,
    [Parameter(Mandatory = $true)][string]$Command
  )

  $vsDevCmd = Get-VsDevCmd
  $cmdLine = "`"$vsDevCmd`" -no_logo -arch=$Arch && $Command"
  Write-Host ">> $cmdLine"
  & cmd.exe /s /c $cmdLine
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code $LASTEXITCODE`: $Command"
  }
}

$matrix = @{
  x64 = @{
    VsArch = "amd64"
    Makefile = "Makefile.msvc"
    DonutArch = "2"
    RunInstance = $true
  }
  x84 = @{
    VsArch = "amd64"
    Makefile = "Makefile.msvc"
    DonutArch = "3"
    RunInstance = $true
  }
  arm64 = @{
    VsArch = "arm64"
    Makefile = "Makefile_arm64.msvc"
    DonutArch = ""
    RunInstance = $false
  }
}

$cfg = $matrix[$Target]

# Fail before touching generated files if the requested MSVC environment is not
# available. The CI runner has a clean checkout; local runs often do not.
Get-VsDevCmd | Out-Null

Remove-Item -Force -ErrorAction SilentlyContinue `
  "instance", "loader.bin", "loader.exe", "donut.exe", "donut_payload_ok.txt", `
  (Join-Path $OutDir "hello.exe"), (Join-Path $OutDir "loader.bin"), (Join-Path $OutDir "instance")

Invoke-VsDevCmd -Arch $cfg.VsArch -Command "nmake /nologo /f $($cfg.Makefile) debug"

$helloExe = Join-Path $OutDir "hello.exe"
Invoke-VsDevCmd -Arch $cfg.VsArch -Command "cl /nologo /W4 /WX /MT /O1 /Fe:`"$helloExe`" ci\hello.c"

$loaderBin = Join-Path $OutDir "loader.bin"
Invoke-VsDevCmd -Arch $cfg.VsArch -Command ".\donut.exe -a $($cfg.DonutArch) -b 1 -e 1 -z 1 -x 1 -i `"$helloExe`" -o `"$loaderBin`""

if (-not (Test-Path "instance")) {
  throw "donut.exe did not create the debug instance file"
}

Copy-Item -Force "instance" (Join-Path $OutDir "instance")

if ($SkipRun -or -not $cfg.RunInstance) {
  Write-Host "Skipping loader.exe instance execution."
  exit 0
}

cmd.exe /c "echo. | loader.exe instance"
if ($LASTEXITCODE -ne 0) {
  throw "loader.exe instance failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path "donut_payload_ok.txt")) {
  throw "The test payload marker file was not created"
}

Move-Item -Force "donut_payload_ok.txt" (Join-Path $OutDir "donut_payload_ok.txt")
Write-Host "Donut $Target debug smoke test passed."
