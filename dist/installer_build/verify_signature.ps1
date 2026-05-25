$ErrorActionPreference = 'Stop'
$machine = 'ABCD-1234-EF56-7890'
$expire = '2036-12-31'
$runs = 12345
$issued = (Get-Date -Format 'yyyy-MM-dd')
$secret = 'GISQC-PRIVATE-KEY-2026-05-NOT-FOR-CLIENT-CHANGE'
Add-Type -TypeDefinition @'
using System;
using System.Text;
public static class Fnv {
  public static ulong Hash(string text) {
    ulong hash = 14695981039346656037UL;
    foreach (byte b in Encoding.UTF8.GetBytes(text)) { hash ^= b; hash *= 1099511628211UL; }
    return hash;
  }
}
'@
$payload = "GISQCWorkbench|$machine|$expire|$runs|$issued|$secret"
$sig = ([Fnv]::Hash($payload).ToString('X16') + [Fnv]::Hash($secret + $payload + $machine).ToString('X16'))
$text = "product=GISQCWorkbench`nmachineCode=$machine`nexpireDate=$expire`nmaxRuns=$runs`nissuedAt=$issued`nsignature=$sig`n"
$tmp='dist\installer_build\manual-license.dat'
Set-Content -Path $tmp -Value $text -Encoding UTF8
Get-Content $tmp
