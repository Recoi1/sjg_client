$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$upstreams = Get-Content (Join-Path $sourceRoot 'upstreams.json') -Raw | ConvertFrom-Json
foreach ($entry in @(@('Petari','Petari','Petari-native.patch'), @('Aurora','aurora','Aurora-mingw.patch'))) {
    $pin = $upstreams.($entry[0])
    $destination = Join-Path $sourceRoot $entry[1]
    if (Test-Path -LiteralPath $destination) { throw "Existing checkout must be reviewed before patching: $destination" }
    git clone --no-checkout $pin.url $destination
    if ($LASTEXITCODE) { throw 'Upstream clone failed' }
    git -C $destination checkout --detach $pin.commit
    if ($LASTEXITCODE) { throw 'Pinned checkout failed' }
    $patch = Join-Path $sourceRoot "patches/$($entry[2])"
    git -C $destination apply --check $patch
    if ($LASTEXITCODE) { throw 'Patch verification failed' }
    git -C $destination apply $patch
    if ($LASTEXITCODE) { throw 'Patch application failed' }
}
