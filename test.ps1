$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 & .\build\fo3check.exe
 if($LASTEXITCODE -ne 0){throw 'FO3 logic checks failed'}
 & .\test-security.ps1
 [xml]$xml=Get-Content 'package/menus/prefabs/LukesItemBrowserFO3/Native.xml' -Raw
 if($xml.rect.name -ne 'LukesItemFO3Native'){throw 'Wrong XML root'}
 foreach($i in 0..255){if(!($xml.rect.image|Where-Object name -eq "R$i") -or !($xml.rect.text|Where-Object name -eq "T$i")){throw "Missing tile $i"}}
 foreach($name in @('White','Cursor','NativeBackground')){
  $data=[IO.File]::ReadAllBytes((Join-Path $PWD "package/textures/interface/LukesItemBrowserFO3/$name.dds"))
  if([Text.Encoding]::ASCII.GetString($data,0,4) -ne 'DDS '){throw 'Invalid texture'}
  $w=[BitConverter]::ToUInt32($data,16);$h=[BitConverter]::ToUInt32($data,12)
  if($data.Length -ne 128+4*$w*$h){throw 'Truncated texture'}
 }
 $imports=Get-Content 'build/imports.txt' -Raw
 if($imports -match '(?i)d3d9.dll|d3d11.dll|vcruntime.*dll|msvcp.*dll'){throw 'Unexpected graphics/VC runtime DLL dependency'}
 $exports=Get-Content 'build/exports.txt' -Raw
 if($exports -notmatch 'FOSEPlugin_Query' -or $exports -notmatch 'FOSEPlugin_Load' -or $exports -match 'NVSEPlugin_'){throw 'Incorrect exports'}
 $sources=(Get-ChildItem src -File -Include *.cpp,*.h -Recurse | Get-Content) -join "`n"
 if($sources -match '0xA012D0|0xA01350|0x11F350C|0xA1B020|InjectUIXML|compileExpression|controlLease'){throw 'New Vegas integration remains'}
 Write-Output 'PASS: FO3 logic, malformed/compressed plugins, native XML/DDS, FOSE exports, static CRT and no graphics DLL dependency. In-game testing remains outstanding.'
 $global:LASTEXITCODE=0
}finally{Pop-Location}
