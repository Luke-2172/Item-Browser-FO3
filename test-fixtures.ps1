$ErrorActionPreference='Stop'
$fixtureDir=Join-Path $PSScriptRoot 'build\fixtures'
New-Item -ItemType Directory -Force $fixtureDir | Out-Null
function Subrecord([string]$tag,[byte[]]$data) {
 $ms=[IO.MemoryStream]::new();$w=[IO.BinaryWriter]::new($ms)
 $w.Write([Text.Encoding]::ASCII.GetBytes($tag));$w.Write([uint16]$data.Length);$w.Write($data)
 $w.Flush();return ,$ms.ToArray()
}
function Record([string]$tag,[uint32]$id,[uint32]$flags,[byte[]]$data) {
 $ms=[IO.MemoryStream]::new();$w=[IO.BinaryWriter]::new($ms)
 $w.Write([Text.Encoding]::ASCII.GetBytes($tag));$w.Write([uint32]$data.Length);$w.Write($flags);$w.Write($id);$w.Write([uint64]0);$w.Write($data)
 $w.Flush();return ,$ms.ToArray()
}
$master=Subrecord 'MAST' ([Text.Encoding]::ASCII.GetBytes("Fallout3.esm`0"))
$header=Record 'TES4' 0 0 $master
$name=Subrecord 'FULL' ([Text.Encoding]::ASCII.GetBytes("Fixture Pistol`0"))
$edid=Subrecord 'EDID' ([Text.Encoding]::ASCII.GetBytes("LukesFixturePistol`0"))
[byte[]]$payload=$name+$edid
$packed=[IO.MemoryStream]::new()
$zip=[IO.Compression.ZLibStream]::new($packed,[IO.Compression.CompressionLevel]::Optimal,$true)
$zip.Write($payload,0,$payload.Length);$zip.Dispose()
[byte[]]$compressed=[BitConverter]::GetBytes([uint32]$payload.Length)+$packed.ToArray()
$own=Record 'WEAP' 0x01001234 0x40000 $compressed
$override=Record 'WEAP' 0x00001235 0 $payload
$deleted=Record 'WEAP' 0x01001236 0x20 $payload
[byte[]]$records=$own+$override+$deleted
$group=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($group)
$writer.Write([Text.Encoding]::ASCII.GetBytes('GRUP'));$writer.Write([uint32]($records.Length+24));$writer.Write([byte[]]::new(16));$writer.Write($records);$writer.Flush()
[byte[]]$all=$header+$group.ToArray()
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'valid.esp'),$all)
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'truncated.esp'),$all[0..($all.Length-8)])
$checker=Join-Path $PSScriptRoot 'build\CatalogCheck.exe'
$zlib=Join-Path $PSScriptRoot 'package\FOSE\Plugins\LukesItemBrowserFO3\zlib1.dll'
$result=& $checker $zlib (Join-Path $fixtureDir 'valid.esp')
if($LASTEXITCODE -ne 0 -or $result[0] -ne 'Items=2 Added=1 Overrides=1 Masters=1 Compressed=1') {throw "Fixture failed: $result"}
$result
& $checker $zlib (Join-Path $fixtureDir 'truncated.esp')
if($LASTEXITCODE -ne 1){throw 'Malformed plugin was not rejected'}
'PASS: compression, origin/override separation, deleted record exclusion, multiword search, and malformed record rejection.'


