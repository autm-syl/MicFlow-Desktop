// MicFlow.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "MicFlow.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <string>
#include <opus/opus.h>
#include "portaudio.h"
#include <Windows.h>
#include <vector>
#include <unordered_set>
#include "SystemAudioSender.h"
#include "Logger.h"
#include <richedit.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include "AboutPage.h"

#ifndef CB_SETMINVISIBLE
#define CB_SETMINVISIBLE 0x1701
#endif

#define MAX_LOADSTRING 100
#define ID_MENU_ABOUT 2001
#define ID_OUTPUT_DEVICE 2002
#define ID_BTN_VBCABLE 2003
#define ID_BTN_IPHONE_APP 2004

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND hBtnStart, hBtnStop;
int g_listenPort = 50005;

HWND hCheckFilter;
HWND hIpInput;
HWND hBtnAddIP;
HWND hListIP;


HWND hBtnStartSend, hBtnStopSend;
HWND hIpInputSend, hPortInputSend, hLogSend;
HWND hBtnClearLog;


HWND hGrpRecv, hLblPortRecv, hLblOutputRecv, hOutputDeviceRecv;
HWND hLblVBCableNote, hBtnVBCable;
HWND hGrpSend, hLblIpSend, hLblPortSend;
HWND hLblLog;
HWND hRichIP;
HWND hBtnIphoneApp;

extern std::atomic<bool> isThreadRunning;
extern std::atomic<bool> sending;

#pragma comment(lib, "shell32.lib")
NOTIFYICONDATAW nid = { 0 };

std::chrono::steady_clock::time_point g_lastAudioTime;
bool g_hasAudioSignal = false;
const int TIMEOUT_MS = 2000; // 2 giây
HWND hIconAudio; // Handle của static control chứa icon
HICON hAudioIcon; // Handle của file icon thực tế

bool useFilter = false;
std::unordered_set<std::string> allowedIPs;
bool g_hasVBCableOutput = false;

#pragma comment(lib, "ws2_32.lib")

HWND hPortInput, hLog;
std::atomic<bool> running(false);
std::thread worker;
DWORD g_uiThreadId = 0;
HWND g_mainWnd = NULL;
const UINT WM_APP_LOG = WM_APP + 1;

#pragma comment(lib, "iphlpapi.lib")
std::string GetLocalIP() {
    ULONG outBufLen = 15000;
    std::vector<BYTE> buffer(outBufLen);
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)buffer.data();

    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST, NULL, pAddresses, &outBufLen) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses; pCurrAddresses != NULL; pCurrAddresses = pCurrAddresses->Next) {
            // Chỉ lấy interface đang hoạt động (Up) và không phải Loopback
            if (pCurrAddresses->OperStatus == IfOperStatusUp && pCurrAddresses->IfType != IF_TYPE_SOFTWARE_LOOPBACK) {
                for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
                    sockaddr_in* sa_in = (sockaddr_in*)pUnicast->Address.lpSockaddr;
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sa_in->sin_addr), ipStr, INET_ADDRSTRLEN);
                    return std::string(ipStr);
                }
            }
        }
    }
    return "127.0.0.1";
}

void AppendLog(const std::string& msg) {
    if (!hLog) return;

    // Nhảy tới cuối văn bản (-1, -1)
    SendMessageA(hLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    // Thay thế vùng chọn (vốn đang ở cuối) bằng nội dung mới
    SendMessageA(hLog, EM_REPLACESEL, FALSE, (LPARAM)msg.c_str());

    // Tự động cuộn xuống dòng cuối cùng để người dùng dễ nhìn
    SendMessageA(hLog, WM_VSCROLL, SB_BOTTOM, NULL);
}

void Log(const std::string& msg) {
    if (g_uiThreadId == 0 || GetCurrentThreadId() == g_uiThreadId) {
        AppendLog(msg);
        return;
    }

    std::string* queuedMsg = new std::string(msg);
    if (!g_mainWnd || !PostMessageA(g_mainWnd, WM_APP_LOG, 0, (LPARAM)queuedMsg)) {
        delete queuedMsg;
    }
}

void SetRichText(HWND hRich, std::string text, COLORREF color) {
    CHARFORMAT2A cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;

    // Chọn phần text vừa chèn để đổi màu
    SendMessage(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessage(hRich, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}
std::wstring s2ws(const std::string& s) {
    int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
    std::vector<wchar_t> buf(len);
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, buf.data(), len);
    return std::wstring(buf.data());
}
void SetRichTextW(HWND hRich, std::wstring text, COLORREF color) {
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;

    // 1. Đưa con trỏ về cuối văn bản (không bôi đen gì cả)
    SendMessageW(hRich, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    // 2. Thiết lập định dạng màu cho vị trí con trỏ hiện tại
    SendMessageW(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 3. Chèn văn bản vào vị trí đó
    SendMessageW(hRich, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}
void UpdateIPDisplay(HWND hRich) {
    // Lấy IP từ hệ thống
    std::string ip = GetLocalIP();
    std::wstring fullIP = s2ws(ip);

    size_t lastDot = fullIP.find_last_of('.');
    if (lastDot == std::wstring::npos) return;

    // Tách thành 3 phần rõ rệt:
    std::wstring prefix = L"PC IP: ";                        // Đen
    std::wstring greenPart = fullIP.substr(0, lastDot + 1); // Xanh (192.168.222.)
    std::wstring lastPart = fullIP.substr(lastDot + 1);    // Đen (120)

    // Xóa trắng RichEdit trước khi vẽ lại
    SetWindowTextW(hRich, L"");

    // Bước 1: Chèn "IP: " màu Đen
    SetRichTextW(hRich, prefix, RGB(0, 0, 0));

    // Bước 2: Chèn 3 cụm đầu màu Xanh lá
    SetRichTextW(hRich, greenPart, RGB(0, 150, 0));

    // Bước 3: Chèn cụm cuối màu Đen
    SetRichTextW(hRich, lastPart, RGB(0, 0, 0));
}




void PopulateOutputDeviceList(HWND hCombo) {
    SendMessageA(hCombo, CB_RESETCONTENT, 0, 0);
    g_hasVBCableOutput = false;

    int defaultItem = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Default speakers/headphones");
    SendMessageA(hCombo, CB_SETITEMDATA, defaultItem, paNoDevice);
    SendMessageA(hCombo, CB_SETCURSEL, defaultItem, 0);

    if (Pa_Initialize() != paNoError) {
        Log("PortAudio init failed while listing devices\r\n");
        return;
    }

    int cableItem = CB_ERR;
    std::unordered_set<std::string> seenOutputNames;
    int numDevices = Pa_GetDeviceCount();
    PaHostApiIndex wasapiHostApi = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    bool hasWasapiOutputs = false;
    if (wasapiHostApi >= 0) {
        for (int i = 0; i < numDevices; i++) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->hostApi == wasapiHostApi && info->maxOutputChannels > 0) {
                hasWasapiOutputs = true;
                break;
            }
        }
    }

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels <= 0) continue;
        if (hasWasapiOutputs && info->hostApi != wasapiHostApi) continue;

        std::string name = info->name;
        if (seenOutputNames.find(name) != seenOutputNames.end()) continue;
        seenOutputNames.insert(name);

        if (name.find("CABLE Input") != std::string::npos) {
            name += " (virtual microphone)";
            g_hasVBCableOutput = true;
        }

        int item = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)name.c_str());
        SendMessageA(hCombo, CB_SETITEMDATA, item, i);

        if (name.find("CABLE Input") != std::string::npos) {
            cableItem = item;
        }
    }

    if (cableItem != CB_ERR) {
        Log("VB-Cable detected. Select it for virtual microphone mode.\r\n");
    } else {
        Log("VB-Cable not detected. Receive mode will still play through normal speakers.\r\n");
    }

    Pa_Terminate();
}

int GetSelectedOutputDevice() {
    int sel = (int)SendMessageA(hOutputDeviceRecv, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return paNoDevice;
    return (int)SendMessageA(hOutputDeviceRecv, CB_GETITEMDATA, sel, 0);
}

void ReceiveLoop(int port, HWND hWnd, int requestedOutputDevice) {

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("WSAStartup failed\r\n");
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        Log("Socket create failed\r\n");
        WSACleanup(); // Đảm bảo cleanup nếu fail nửa chừng
        return;
    }

    // --- FIX 1: Cho phép dùng lại Port ngay lập tức ---
    int opt = 1;
    struct linger sl;
    sl.l_onoff = 1;  // Kích hoạt Linger
    sl.l_linger = 0; // Thời gian đợi = 0 (Ép đóng cứng)
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&sl, sizeof(sl));

    DWORD recvTimeoutMs = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeoutMs, sizeof(recvTimeoutMs));


    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        Log("Bind failed: " + std::to_string(WSAGetLastError()) + "\r\n");
        shutdown(sock, SD_BOTH); // Ngắt kết nối trước
        closesocket(sock); // Đóng ngay nếu bind xịt
        WSACleanup();
        return;
    }

    Log("Listening OK...\r\n");

    // ======================
    // 🎧 INIT OPUS DECODER
    // ======================
    int opusErr;
    OpusDecoder* decoder = opus_decoder_create(48000, 1, &opusErr);

    if (opusErr != OPUS_OK) {
        Log("Opus decoder init failed\r\n");
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        WSACleanup();
        return;
    }

    // ======================
    // 🔊 INIT PORTAUDIO
    // ======================
    if (Pa_Initialize() != paNoError) {
        Log("PortAudio init failed\r\n");
        opus_decoder_destroy(decoder);
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        WSACleanup();
        return;
    }

    int deviceIndex = requestedOutputDevice;
    if (deviceIndex == paNoDevice) {
        deviceIndex = Pa_GetDefaultOutputDevice();
    }

    const PaDeviceInfo* outputInfo = (deviceIndex == paNoDevice) ? NULL : Pa_GetDeviceInfo(deviceIndex);
    if (!outputInfo || outputInfo->maxOutputChannels <= 0) {
        Log("No usable output device found\r\n");
        Pa_Terminate();
        opus_decoder_destroy(decoder);
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        WSACleanup();
        return;
    }

    Log("Output device: " + std::string(outputInfo->name) + "\r\n");
    if (std::string(outputInfo->name).find("CABLE Input") == std::string::npos) {
        Log("Normal speaker mode. Select CABLE Input only when you need virtual microphone mode.\r\n");
    }

    PaStreamParameters outputParams;
    outputParams.device = deviceIndex;
    outputParams.channelCount = 1;
    outputParams.sampleFormat = paInt16;
    outputParams.suggestedLatency = outputInfo->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = NULL;

    PaStream* stream = NULL;

    if (Pa_OpenStream(
        &stream,
        NULL,
        &outputParams,
        48000,
        960,
        paNoFlag,
        NULL,
        NULL
    ) != paNoError) {
        Log("PortAudio open failed\r\n");
        Pa_Terminate();
        opus_decoder_destroy(decoder);
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        WSACleanup();
        return;
    }

    if (Pa_StartStream(stream) != paNoError) {
        Log("PortAudio start failed\r\n");
        Pa_CloseStream(stream);
        Pa_Terminate();
        opus_decoder_destroy(decoder);
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        WSACleanup();
        return;
    }

    // ======================
    // 🔁 MAIN LOOP
    // ======================
    char buffer[1024];

    while (running) {
        sockaddr_in client{};
        int lenAddr = sizeof(client);

        int len = recvfrom(sock,
            buffer,
            sizeof(buffer),
            0,
            (sockaddr*)&client,
            &lenAddr);

        if (len == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
            Log("Receive failed: " + std::to_string(err) + "\r\n");
            break;
        }

        if (!running) break;

        if (len > 0) {
            //Log("Got data len=" + std::to_string(len) + "\r\n");
            // 1. Đánh dấu có tín hiệu
            g_hasAudioSignal = true;

            // 2. Cập nhật mốc thời gian cuối cùng nhận được data
            g_lastAudioTime = std::chrono::steady_clock::now();

            // =========================
        // 🔥 LẤY IP SENDER
        // =========================
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client.sin_addr, ipStr, sizeof(ipStr));

            std::string senderIP(ipStr);

            // =========================
            // 🔒 FILTER IP (ĐẶT Ở ĐÂY)
            // =========================
            if (useFilter) {
                if (useFilter && allowedIPs.find(senderIP) == allowedIPs.end()) {
                    Log("❌ Block IP: " + senderIP + "\r\n");
                    continue;
                }
            }

            //Log("✅ Accepted IP: " + senderIP + "\r\n");



            short pcm[960];

            int frame = opus_decode(
                decoder,
                (unsigned char*)buffer,
                len,
                pcm,
                960,
                0
            );

            if (frame > 0) {
                //Pa_WriteStream(stream, pcm, frame);
                PaError err = Pa_WriteStream(stream, pcm, frame);
                if (err == paOutputUnderflowed) {
                    Log("⚠️ Audio underflow\r\n");
                }
            }
        }

        // 👇 ĐẶT Ở ĐÂY
        //std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // ======================
    // 🧹 CLEANUP
    // ======================
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    opus_decoder_destroy(decoder);

    shutdown(sock, SD_BOTH); // Ngắt kết nối trước
    closesocket(sock);
    WSACleanup();

    Log("Stopped\r\n");
}


// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    g_uiThreadId = GetCurrentThreadId();
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MICFLOW, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MICFLOW));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}


std::string GetPCName() {
    char name[256];
    DWORD size = 256;
    GetComputerNameA(name, &size);
    return std::string(name);
}

void DiscoveryLoop() {
    // 1. Khởi tạo Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    // 2. Tạo UDP Socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    // 3. Cấu hình địa chỉ lắng nghe (Port 22500)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(22500);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return;
    }

    char buffer[256];
    Log("Listening on port 22500 (Efficient Mode)...\r\n");

    // 4. Vòng lặp vô tận
    // recvfrom ở đây sẽ chặn thread lại cho đến khi có dữ liệu -> Tiết kiệm CPU tối đa
    while (true) {
        sockaddr_in client{};
        int lenAddr = sizeof(client);

        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&client, &lenAddr);

        if (len > 0) {
            std::string msg(buffer, len);
            if (msg == "DISCOVER_MICFLOW") {
                std::string pcName = GetPCName();
                std::string reply = "MICFLOW:" + pcName + ":" + std::to_string(g_listenPort);

                sendto(sock, reply.c_str(), (int)reply.size(), 0, (sockaddr*)&client, lenAddr);
                Log("Discovery: Responded to " + msg + "\r\n");
            }
            if (msg == "START_AUDIO_MICFLOW") {
              
                if (IsSendingAudio()) {
                    Log("⏳ Stopping thread, please wait...\r\n");

                    // Hàm này sẽ đợi thread kết thúc xong mới chạy tiếp
                    StopSendAudio();

                    // doi 2s 
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    char ip[64];
                    char portStr[16];
                    GetWindowTextA(hIpInputSend, ip, 64);
                    GetWindowTextA(hPortInputSend, portStr, 16);
                    int port = atoi(portStr);

                    StartSendAudio(ip, port);
                }
            }
        }
        else if (len == SOCKET_ERROR) {
            // Nếu socket bị lỗi nghiêm trọng, thoát vòng lặp để tránh treo CPU
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT) break;
        }
    }

    closesocket(sock);
    WSACleanup();
}



void SaveIPToRegistry(const std::string& ip) {
    HKEY hKey;
    // Sử dụng KEY_SET_VALUE thay vì KEY_WRITE để giảm yêu cầu đặc quyền
    LSTATUS status = RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\AudioStreamer", 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);

    if (status == ERROR_SUCCESS) {
        status = RegSetValueExA(hKey, "LastIP", 0, REG_SZ,
            (const BYTE*)ip.c_str(), (DWORD)(ip.length() + 1));

        if (status == ERROR_SUCCESS) {
            Log("✅ Registry: Saved IP " + ip + "\r\n");
        }
        else {
            Log("RegSetValueEx failed: " + std::to_string(status) + "\r\n");
        }
        RegCloseKey(hKey);
    }
    else {
        Log("RegCreateKeyEx failed: " + std::to_string(status) + "\r\n");
    }
}

std::string LoadIPFromRegistry() {
    char buffer[255] = { 0 };
    DWORD bufferSize = sizeof(buffer);
    HKEY hKey;

    LSTATUS status = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\AudioStreamer", 0, KEY_QUERY_VALUE, &hKey);

    if (status == ERROR_SUCCESS) {
        status = RegQueryValueExA(hKey, "LastIP", NULL, NULL, (LPBYTE)buffer, &bufferSize);
        RegCloseKey(hKey);

        if (status == ERROR_SUCCESS) {
            std::string ip(buffer);
            Log("✅ Registry: Loaded IP " + ip + "\r\n");
            return ip;
        }
        else {
            Log("RegQueryValueEx failed: " + std::to_string(status) + "\r\n");
        }
    }
    else {
        // ERROR_FILE_NOT_FOUND (2) là bình thường trong lần đầu chạy app
        if (status != ERROR_FILE_NOT_FOUND) {
            Log("RegOpenKeyEx failed: " + std::to_string(status) + "\r\n");
        }
    }
    return "192.168.1.1"; // Mặc định
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MICFLOW));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_MICFLOW);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    // Tạo cửa sổ với Menu mặc định từ Resource
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return FALSE;

    // 1. Lấy handle của thanh Menu chính
    HMENU hMenuBar = GetMenu(hWnd);

    if (hMenuBar) {
        // 2. Lấy handle của Menu con "Help" (thường nằm ở vị trí cuối cùng)
        // Nếu menu của bạn là [File] [Help] thì index là 1. 
        // Nếu là [File] [Edit] [Help] thì index là 2.
        int helpMenuIndex = GetMenuItemCount(hMenuBar) - 1;
        HMENU hHelpMenu = GetSubMenu(hMenuBar, helpMenuIndex);

        if (hHelpMenu) {
            // 3. Thêm một đường gạch ngang phân cách (tùy chọn)
            AppendMenuW(hHelpMenu, MF_SEPARATOR, 0, NULL);

            // 4. Thêm mục "Hướng dẫn sử dụng" vào cuối menu Help
            AppendMenuW(hHelpMenu, MF_STRING, ID_MENU_ABOUT, L"MicFlow Guide");
        }

        // Bắt buộc vẽ lại thanh menu để cập nhật giao diện
        DrawMenuBar(hWnd);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}


//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LoadLibraryA("Msftedit.dll");
    static HFONT hFontIP = NULL;
   


    switch (message)
    {
    case WM_APP_LOG:
    {
        std::string* queuedMsg = (std::string*)lParam;
        if (queuedMsg) {
            AppendLog(*queuedMsg);
            delete queuedMsg;
        }
    }
    break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);

            if (wmId == 1) { // Start
                if (!running) {
                    char buf[16];
                    GetWindowTextA(hPortInput, buf, 16);
                    int port = atoi(buf);
                    // ❗ check port 22500
                    if (port == 22500) {
                        MessageBoxA(hWnd,
                            "Port 22500 is already in use.\nPlease choose a different port!",
                            "Error",
                            MB_OK | MB_ICONERROR);
                        return 0;
                    }

                    running = true;

                    g_listenPort = port;
                    int outputDevice = GetSelectedOutputDevice();
                    worker = std::thread(ReceiveLoop, port, hWnd, outputDevice);


                    // 🔥 disable Start, enable Stop
                    EnableWindow(hBtnStart, FALSE);
                    EnableWindow(hBtnStop, TRUE);
                }
            }

            if (wmId == 2) {
                running = false;
                if (worker.joinable()) {
                    worker.join();
                }
                // 🔥 enable Start, disable Stop
                EnableWindow(hBtnStart, TRUE);
                EnableWindow(hBtnStop, FALSE);
            }
            if (wmId == 10) {

                useFilter = (SendMessage(hCheckFilter, BM_GETCHECK, 0, 0) == BST_CHECKED);

                EnableWindow(hIpInput, useFilter);
                EnableWindow(hBtnAddIP, useFilter);
                EnableWindow(hListIP, useFilter);

                Log(useFilter ? "✅ Filter ON\r\n" : "❌ Filter OFF\r\n");
            }
            if (wmId == 11) {

                char buf[64];
                GetWindowTextA(hIpInput, buf, 64);

                std::string ip(buf);

                if (ip.empty()) return 0;

                // tránh trùng
                if (allowedIPs.find(ip) != allowedIPs.end()) {
                    return 0; // đã tồn tại
                }

                allowedIPs.insert(ip);

                SendMessageA(hListIP, LB_ADDSTRING, 0, (LPARAM)ip.c_str());

                SetWindowTextA(hIpInput, "");

                Log("➕ Added IP: " + ip + "\r\n");
            }
            if (wmId == 12 && HIWORD(wParam) == LBN_DBLCLK) {

                int sel = SendMessage(hListIP, LB_GETCURSEL, 0, 0);

                if (sel != LB_ERR) {

                    char buf[64];
                    SendMessageA(hListIP, LB_GETTEXT, sel, (LPARAM)buf);

                    std::string ip(buf);

                    // remove khỏi vector
                    allowedIPs.erase(ip);

                    // remove khỏi UI
                    SendMessage(hListIP, LB_DELETESTRING, sel, 0);

                    Log("❌ Removed IP: " + ip + "\r\n");
                }
            }
            if (wmId == ID_OUTPUT_DEVICE && HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessageA(hOutputDeviceRecv, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    char deviceName[256] = { 0 };
                    SendMessageA(hOutputDeviceRecv, CB_GETLBTEXT, sel, (LPARAM)deviceName);
                    Log("Selected output device: " + std::string(deviceName) + "\r\n");
                }
            }
            if (wmId == ID_BTN_VBCABLE) {
                ShellExecuteW(NULL, L"open", L"https://vb-audio.com/Cable/index.htm", NULL, NULL, SW_SHOWNORMAL);
            }
            if (wmId == ID_BTN_IPHONE_APP) {
                ShellExecuteW(NULL, L"open", L"https://apps.apple.com/vn/app/micflow-wifi-audio-stream/id6766181770", NULL, NULL, SW_SHOWNORMAL);
            }

            /////////////////////////////
            if (wmId == 30) { // Start send

                if (!IsSendingAudio()) {
                    char ip[64];
                    char portStr[16];
                    GetWindowTextA(hIpInputSend, ip, 64);
                    GetWindowTextA(hPortInputSend, portStr, 16);
                    int port = atoi(portStr);

                    StartSendAudio(ip, port);

                    // Cập nhật UI ngay lập tức
                    EnableWindow(hBtnStartSend, FALSE);
                    EnableWindow(hBtnStopSend, TRUE);
                    Log("🚀 Sending to phone started...\r\n");
                    SaveIPToRegistry(std::string(ip));
                }
            }

            if (wmId == 31) {
                if (IsSendingAudio()) {
                    Log("⏳ Stopping thread, please wait...\r\n");

                    // Hàm này sẽ đợi thread kết thúc xong mới chạy tiếp
                    StopSendAudio();

                    // Cập nhật UI
                    EnableWindow(hBtnStartSend, TRUE);
                    EnableWindow(hBtnStopSend, FALSE);

                    // Chạy Timer để kiểm tra khi nào thread thoát hẳn
                    SetTimer(hWnd, 101, 100, NULL);
                }
            }
            if (wmId == 50) {
                SetWindowTextA(hLog, "");
            }
            if (wmId == ID_MENU_ABOUT) { 
                ShowAboutPage(hWnd, hInst);
            }


            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
    break;
    case WM_TIMER:
        if (wParam == 101) {
            // Kiểm tra biến extern từ file SystemAudioSender
            if (!isThreadRunning) {
                KillTimer(hWnd, 101);
                EnableWindow(hBtnStartSend, TRUE);
                EnableWindow(hBtnStopSend, FALSE);
                Log("Cleanup thread finished. Ready to restart.\r\n");
            }
        }

        if (wParam == 100) {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastAudioTime).count();

            if (duration > 2000) g_hasAudioSignal = false;

            static bool flashState = false;
            if (g_hasAudioSignal) {
                flashState = !flashState;
                ShowWindow(hIconAudio, flashState ? SW_SHOW : SW_HIDE);
            }
            else {
                ShowWindow(hIconAudio, SW_HIDE);
            }
        }

        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        //
        if (sending || isThreadRunning) {
            sending = false; // 1. Ra lệnh cho luồng SendLoop dừng lại

            // 2. Chờ tối đa 1 giây để luồng tự dọn dẹp
            int timeout = 0;
            while (isThreadRunning && timeout < 200) {
                // Xử lý thông điệp Windows để tránh treo UI trong khi đợi
                MSG msg;
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
                Sleep(10);
                timeout++;
            }
        }

        running = false;
        if (worker.joinable()) worker.join();
        g_mainWnd = NULL;
        PostQuitMessage(0);
        DeleteObject(hFontIP);
        KillTimer(hWnd, 100);
        break;
    case WM_CLOSE:

        ShowWindow(hWnd, SW_HIDE);
        return 0;

        //DestroyWindow(hWnd); // Sau khi luồng thoát mới thực sự hủy cửa sổ
        //break;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;

        if (hwndStatic == hIconAudio) {
            SetTextColor(hdcStatic, RGB(235, 11, 35)); // Màu xanh lá rực rỡ
            SetBkMode(hdcStatic, TRANSPARENT);      // Nền trong suốt
            return (INT_PTR)GetStockObject(NULL_BRUSH); // Trả về brush rỗng để không đè nền
        }
    }
    break;
    case WM_CREATE:
    {
        g_mainWnd = hWnd;

        // 1. Tạo Font chữ to và đậm (Segoe UI, cỡ 24, Bold)
        hFontIP = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        // 2. 📟 RICHEDIT HIỂN THỊ IP (ĐẶT LÊN ĐẦU TIÊN)
        // Tọa độ Y=5, chiều cao 35 để chữ to không bị cắt
        hRichIP = CreateWindowA("RICHEDIT50W", "",
            WS_VISIBLE | WS_CHILD | ES_READONLY,
            10, 5, 255, 35, hWnd, NULL, NULL, NULL);

        // Áp dụng font cho RichEdit
        SendMessage(hRichIP, WM_SETFONT, (WPARAM)hFontIP, TRUE);

        hBtnIphoneApp = CreateWindowW(L"BUTTON", L"App on iPhone (Apple's Store)",
            WS_VISIBLE | WS_CHILD,
            275, 5, 240, 28, hWnd, (HMENU)ID_BTN_IPHONE_APP, NULL, NULL);

        // Dịch chuyển tất cả GroupBox xuống bên dưới dòng IP (Y bắt đầu từ 50)
        int offset = 45;

        // =========================
        // 📦 GROUP RECEIVE (LEFT)
        // =========================
        hGrpRecv = CreateWindowA("BUTTON", "Receive (Phone -> PC)",
            WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
            10, 10 + offset, 200, 260, hWnd, NULL, NULL, NULL);

        hLblPortRecv = CreateWindowA("STATIC", "Port:",
            WS_VISIBLE | WS_CHILD,
            20, 35 + offset, 40, 20, hWnd, NULL, NULL, NULL);

        hPortInput = CreateWindowA("EDIT", "50005",
            WS_VISIBLE | WS_CHILD | WS_BORDER,
            60, 32 + offset, 135, 22, hWnd, NULL, NULL, NULL);

        hBtnStart = CreateWindowA("BUTTON", "Start",
            WS_VISIBLE | WS_CHILD,
            20, 125 + offset, 80, 30, hWnd, (HMENU)1, NULL, NULL);

        hBtnStop = CreateWindowA("BUTTON", "Stop",
            WS_VISIBLE | WS_CHILD,
            115, 125 + offset, 80, 30, hWnd, (HMENU)2, NULL, NULL);

        EnableWindow(hBtnStop, FALSE);

        // ===== FILTER =====
        hCheckFilter = CreateWindowA("BUTTON", "Enable IP Filter",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            20, 165 + offset, 150, 20, hWnd, (HMENU)10, NULL, NULL);

        hIpInput = CreateWindowA("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER,
            20, 190 + offset, 120, 22, hWnd, NULL, NULL, NULL);

        hBtnAddIP = CreateWindowA("BUTTON", "Add",
            WS_VISIBLE | WS_CHILD,
            145, 190 + offset, 50, 22, hWnd, (HMENU)11, NULL, NULL);

        hListIP = CreateWindowA("LISTBOX", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY,
            20, 220 + offset, 175, 30, hWnd, (HMENU)12, NULL, NULL);

        EnableWindow(hIpInput, FALSE);
        EnableWindow(hBtnAddIP, FALSE);
        EnableWindow(hListIP, FALSE);


        // 1. Tạo Static Control với ký tự dấu chấm tròn to
        hIconAudio = CreateWindowW(L"STATIC", L"●",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            195, 48 + offset, 40, 40, hWnd, NULL, NULL, NULL);

        // 2. Tạo Font to cho dấu chấm (nếu muốn nó to rõ hơn)
        HFONT hFontDot = CreateFontA(40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Arial");
        SendMessage(hIconAudio, WM_SETFONT, (WPARAM)hFontDot, TRUE);

        // =========================
        // 📦 GROUP SEND (RIGHT)
        // =========================
        hGrpSend = CreateWindowA("BUTTON", "Send (PC -> phone)",
            WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
            220, 10 + offset, 200, 260, hWnd, NULL, NULL, NULL);

        hLblIpSend = CreateWindowA("STATIC", "Phone IP:",
            WS_VISIBLE | WS_CHILD,
            230, 35 + offset, 80, 20, hWnd, NULL, NULL, NULL);

        std::string oldIp = LoadIPFromRegistry();

        hIpInputSend = CreateWindowA("EDIT", oldIp.c_str(),
            WS_VISIBLE | WS_CHILD | WS_BORDER,
            305, 32 + offset, 120, 22, hWnd, NULL, NULL, NULL);

        hLblPortSend = CreateWindowA("STATIC", "Port:",
            WS_VISIBLE | WS_CHILD,
            230, 65 + offset, 80, 20, hWnd, NULL, NULL, NULL);

        hPortInputSend = CreateWindowA("EDIT", "50005",
            WS_VISIBLE | WS_CHILD | WS_BORDER,
            305, 62 + offset, 120, 22, hWnd, NULL, NULL, NULL);

        hBtnStartSend = CreateWindowA("BUTTON", "Start Send",
            WS_VISIBLE | WS_CHILD,
            230, 105 + offset, 80, 30, hWnd, (HMENU)30, NULL, NULL);

        hBtnStopSend = CreateWindowA("BUTTON", "Stop",
            WS_VISIBLE | WS_CHILD,
            325, 105 + offset, 80, 30, hWnd, (HMENU)31, NULL, NULL);

        EnableWindow(hBtnStopSend, FALSE);

        hLblOutputRecv = CreateWindowA("STATIC", "Output:",
            WS_VISIBLE | WS_CHILD,
            230, 58 + offset, 60, 20, hWnd, NULL, NULL, NULL);

        hOutputDeviceRecv = CreateWindowA("COMBOBOX", "",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            285, 58 + offset, 285, 240, hWnd, (HMENU)ID_OUTPUT_DEVICE, NULL, NULL);
        SendMessage(hOutputDeviceRecv, CB_SETMINVISIBLE, 6, 0);

        hLblVBCableNote = CreateWindowA("STATIC", "To use as a microphone, install VB-Cable.",
            WS_VISIBLE | WS_CHILD,
            285, 86 + offset, 185, 32, hWnd, NULL, NULL, NULL);

        hBtnVBCable = CreateWindowA("BUTTON", "VB-Cable",
            WS_VISIBLE | WS_CHILD,
            470, 90 + offset, 90, 24, hWnd, (HMENU)ID_BTN_VBCABLE, NULL, NULL);


        // =========================
        // 📜 LOG (BOTTOM)
        // =========================
        // Phần Log cũng cần dịch xuống để không đè lên phần Send/Receive
        hLblLog = CreateWindowA("STATIC", "Log:",
            WS_VISIBLE | WS_CHILD,
            10, 280 + offset, 50, 20, hWnd, NULL, NULL, NULL);

        hLog = CreateWindowA("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 300 + offset, 410, 150, hWnd, NULL, NULL, NULL);

        hBtnClearLog = CreateWindowA("BUTTON", "Clear",
            WS_VISIBLE | WS_CHILD,
            60, 278 + offset, 50, 20,
            hWnd, (HMENU)50, NULL, NULL);

        PopulateOutputDeviceList(hOutputDeviceRecv);
        ShowWindow(hLblVBCableNote, g_hasVBCableOutput ? SW_HIDE : SW_SHOW);
        ShowWindow(hBtnVBCable, g_hasVBCableOutput ? SW_HIDE : SW_SHOW);

        // 🔍 Discovery thread
        std::thread discoveryThread(DiscoveryLoop);
        discoveryThread.detach();

        // 🎨 Cập nhật IP ngay khi khởi tạo
        UpdateIPDisplay(hRichIP);
        // 3. Chạy một Timer để kiểm tra trạng thái nhấp nháy (500ms một lần)
        SetTimer(hWnd, 100, 500, NULL);



        // Thiết lập cho System Tray Icon
        nid.cbSize = sizeof(NOTIFYICONDATAW); // Dùng sizeof bản W
        nid.hWnd = hWnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = 9527;

        // Load Icon
        nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL));

        // Copy chuỗi Unicode (L"...")
        wcscpy_s(nid.szTip, L"MicFlow Background");

        // 3. Gọi đích danh hàm bản W
        Shell_NotifyIconW(NIM_ADD, &nid);
    }
    break;

    case 9527: // Đây là uCallbackMessage bạn đã đặt
        switch (lParam) {
            case WM_LBUTTONDBLCLK: // Double click chuột trái để hiện lại
            case WM_LBUTTONUP:     // Hoặc chỉ cần click chuột trái một lần
                ShowWindow(hWnd, SW_SHOW);       // Hiện cửa sổ
                ShowWindow(hWnd, SW_RESTORE);    // Khôi phục nếu đang bị minimize
                SetForegroundWindow(hWnd);       // Đưa lên trên cùng
                break;

            case WM_RBUTTONUP: // Click chuột phải để hiện Menu (Show/Exit)
            {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1001, L"Show App");
                AppendMenuW(hMenu, MF_STRING, 1002, L"Exit MicFlow");

                // Bắt buộc gọi SetForegroundWindow trước khi hiện Menu 
                // để Menu tự đóng khi click ra ngoài.
                SetForegroundWindow(hWnd);

                int selection = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);

                if (selection == 1001) {
                    ShowWindow(hWnd, SW_SHOW);
                    SetForegroundWindow(hWnd);
                }
                else if (selection == 1002) {
                    DestroyWindow(hWnd); // Thoát hoàn toàn
                }
                DestroyMenu(hMenu);
            }
            break;
        }
        break;


    case WM_SIZE:
    {
        int cx = LOWORD(lParam);
        int cy = HIWORD(lParam);

        if (cx == 0 || cy == 0) return 0;

        // Cấu hình các thông số khoảng cách
        int padding = 10;
        int ipHeight = 35;      // Chiều cao cho dòng RichEdit IP
        int offset = 45;        // Khoảng cách từ đỉnh Window đến bắt đầu của các GroupBox
        int topHeight = 260;    // Chiều cao cố định của 2 nhóm (Receive/Send)
        int halfWidth = (cx - padding * 3) / 2;

        // 1. Cố định vị trí dòng IP to trên cùng
        //int iphoneBtnWidth = 240;
        //MoveWindow(hRichIP, padding, 5, cx - padding * 3 - iphoneBtnWidth, ipHeight, TRUE);
        //MoveWindow(hBtnIphoneApp, cx - padding - iphoneBtnWidth - 650, 8, iphoneBtnWidth, 28, TRUE);

        // ==================== CANH CHỈNH LEFT GROUP ====================
        // Y bắt đầu từ offset (45) thay vì padding (10)
        MoveWindow(hGrpRecv, padding, offset, halfWidth, topHeight, TRUE);

        MoveWindow(hLblPortRecv, padding + 10, offset + 25, 60, 20, TRUE);
        MoveWindow(hPortInput, padding + 50, offset + 22, halfWidth - 65, 22, TRUE);

        MoveWindow(hBtnStart, padding + 10, offset + 55, 80, 30, TRUE);
        MoveWindow(hBtnStop, padding + 100, offset + 55, 80, 30, TRUE);

        MoveWindow(hCheckFilter, padding + 10, offset + 95, 150, 20, TRUE);

        MoveWindow(hIpInput, padding + 10, offset + 120, halfWidth - 75, 22, TRUE);
        MoveWindow(hBtnAddIP, padding + halfWidth - 60, offset + 120, 50, 22, TRUE);
        MoveWindow(hListIP, padding + 10, offset + 150, halfWidth - 20, 100, TRUE);

        // ==================== CANH CHỈNH RIGHT GROUP ====================
        int rightStartX = padding * 2 + halfWidth;
        MoveWindow(hGrpSend, rightStartX, offset, halfWidth, topHeight, TRUE);

        MoveWindow(hLblIpSend, rightStartX + 10, offset + 25, 70, 20, TRUE);
        MoveWindow(hIpInputSend, rightStartX + 95, offset + 22, halfWidth - 115, 22, TRUE);

        MoveWindow(hLblPortSend, rightStartX + 10, offset + 55, 70, 20, TRUE);
        MoveWindow(hPortInputSend, rightStartX + 95, offset + 52, halfWidth - 115, 22, TRUE);

        MoveWindow(hBtnStartSend, rightStartX + 10, offset + 95, 80, 30, TRUE);
        MoveWindow(hBtnStopSend, rightStartX + 100, offset + 95, 80, 30, TRUE);

        MoveWindow(hLblOutputRecv, 230, offset + 58, 50, 20, TRUE);
        MoveWindow(hOutputDeviceRecv, 285, offset + 58, halfWidth - 450, 240, TRUE);
        MoveWindow(hLblVBCableNote,285, offset + 86, halfWidth - 450, 32, TRUE);
        MoveWindow(hBtnVBCable, 285 + halfWidth - 450, offset + 90, 90, 24, TRUE);

        // ==================== CANH CHỈNH LOG AREA ====================
        // logStartY phải tính từ offset + topHeight
        int logStartY = offset + topHeight + padding;

        // Căn chỉnh tiêu đề Log và nút Clear
        MoveWindow(hLblLog, padding, logStartY, 50, 20, TRUE);
        MoveWindow(hBtnClearLog, padding + 55, logStartY - 2, 60, 22, TRUE);

        // Khung Log chiếm phần còn lại của chiều cao Window
        int logHeight = cy - logStartY - 30; // 30 là khoảng trừ hao lề dưới
        if (logHeight > 0) {
            MoveWindow(hLog, padding, logStartY + 25, cx - padding * 2, logHeight, TRUE);
        }
    }
    return 0;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    
    
    return 0;
}


// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


