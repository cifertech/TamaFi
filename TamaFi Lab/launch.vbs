Set sh = CreateObject("Wscript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
folder = fso.GetParentFolderName(WScript.ScriptFullName)
sh.CurrentDirectory = folder
exe = folder & "\dist\TamaFiLab\TamaFiLab.exe"
If fso.FileExists(exe) Then
  sh.CurrentDirectory = folder & "\dist\TamaFiLab"
  sh.Run """" & exe & """", 1, False
Else
  sh.Run "py -3 -m pip install -r requirements.txt -q", 0, True
  sh.Run "pyw -3 tamafi_lab.py", 0, False
End If
