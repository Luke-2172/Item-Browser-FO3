$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-fixtures.ps1')
function Reject([string]$name,[byte[]]$data){
 $path=Join-Path $fixtureDir $name
 [IO.File]::WriteAllBytes($path,$data)
 $result=& $checker $zlib $path 2>&1
 if($LASTEXITCODE -ne 1){throw "Failed to reject $name"}
 Write-Output "PASS $name : $result"
}
$clean=Record 'TES4' 0 0 ([byte[]]@())
Reject 'master-traversal.esp' (Record 'TES4' 0 0 (Subrecord 'MAST' ([Text.Encoding]::ASCII.GetBytes("..\outside.esm`0"))))
Reject 'master-injection.esp' (Record 'TES4' 0 0 (Subrecord 'MAST' ([Text.Encoding]::ASCII.GetBytes("[Request].esm`0"))))
Reject 'huge-inflate.esp' ($clean+(Record 'WEAP' 0x800 0x40000 ([BitConverter]::GetBytes([uint32]::MaxValue))))
Reject 'long-name.esp' ($clean+(Record 'WEAP' 0x800 0 (Subrecord 'FULL' ([byte[]]::new(4097)))))
[byte[]]$masters=@()
for($n=0;$n -lt 255;$n++){$masters+=Subrecord 'MAST' ([Text.Encoding]::ASCII.GetBytes("Master$n.esm`0"))}
Reject 'too-many-masters.esp' (Record 'TES4' 0 0 $masters)
Reject 'dangling-extended.esp' ($clean+(Record 'WEAP' 0x800 0 (Subrecord 'XXXX' ([BitConverter]::GetBytes([uint32]10)))))
