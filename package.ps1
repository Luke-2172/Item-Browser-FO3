$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$version='1.0'
$dist=Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $dist|Out-Null
function ZipFiles($target,$entries){
 $stream=[IO.File]::Open($target,[IO.FileMode]::Create)
 $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
 try{foreach($entry in $entries){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$entry.Path,$entry.Name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}
 finally{$zip.Dispose();$stream.Dispose()}
}
$package=Join-Path $PSScriptRoot 'package'
$required=@(
 'FOSE/Plugins/LukesItemBrowserFO3.dll',
 'FOSE/Plugins/LukesItemBrowserFO3.ini',
 'FOSE/Plugins/LukesItemBrowserFO3/zlib1.dll',
 'menus/prefabs/LukesItemBrowserFO3/Native.xml',
 'textures/interface/LukesItemBrowserFO3/White.dds',
 'textures/interface/LukesItemBrowserFO3/Cursor.dds',
 'textures/interface/LukesItemBrowserFO3/NativeBackground.dds'
)
$runtime=@(foreach($name in $required){
 $path=Join-Path $package $name
 if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing runtime file: $name"}
 @{Path=$path;Name=$name}
})
$runtime+=@{Path=(Join-Path $PSScriptRoot 'third_party/zlib-LICENSE.txt');Name='zlib-LICENSE.txt'}
ZipFiles (Join-Path $dist "Lukes-Item-Browser-FO3-$version.zip") $runtime
$source=@(Get-ChildItem $PSScriptRoot -Recurse -File|Where-Object {
 $rel=$_.FullName.Substring($PSScriptRoot.Length+1)
 $rel -notmatch '^(build|dist)[\\/]' -and $_.Extension -notin @('.dll','.exe','.obj','.lib','.exp','.log') -and $_.Name -notlike '*-status.ini'
}|ForEach-Object {@{Path=$_.FullName;Name=$_.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/')}})
ZipFiles (Join-Path $dist "Lukes-Item-Browser-FO3-Source-$version.zip") $source
Get-ChildItem $dist -Filter *.zip | Select-Object Name,Length
