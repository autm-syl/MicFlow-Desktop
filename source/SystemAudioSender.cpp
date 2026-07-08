// SystemAudioSender.cpp

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include <opus/opus.h>

#include "SystemAudioSender.h"
#include "Logger.h"

// WASAPI
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <vector>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")


std::atomic<bool> sending(false);
std::atomic<bool> isThreadRunning(false);
std::thread sendThread;
bool mutePCState = true; // Biến option từ UI của bạn


void SendLoop(std::string ip, int port) {
    isThreadRunning = true; // Báo hiệu luồng bắt đầu chạy
    // =====================
    // WINSOCK
    // =====================
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    // =====================
    // WASAPI INIT
    // =====================
    CoInitialize(NULL);

    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;

    CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator
    );

    pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

    IAudioClient* pAudioClient = NULL;

    pDevice->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        NULL,
        (void**)&pAudioClient
    );

    WAVEFORMATEX* pwfx;
    REFERENCE_TIME bufferDuration = 10000000 / 50; // 20ms
    pAudioClient->GetMixFormat(&pwfx);

    pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        bufferDuration,
        0,
        pwfx,
        NULL
    );

   
    IAudioCaptureClient* pCaptureClient;

    pAudioClient->GetService(
        __uuidof(IAudioCaptureClient),
        (void**)&pCaptureClient
    );

    pAudioClient->Start();

    Log("Capture started\r\n");
    // Log thông số để debug
    Log("--- Windows Audio Format ---\r\n");
    Log("Sample Rate: " + std::to_string(pwfx->nSamplesPerSec) + " Hz\r\n");
    Log("Channels: " + std::to_string(pwfx->nChannels) + "\r\n");
    Log("Bits Per Sample: " + std::to_string(pwfx->wBitsPerSample) + "\r\n");

    if (pwfx->nSamplesPerSec != 48000) {
        Log("WARNING: Sample Rate is not 48kHz. Opus may fail or audio may be distorted!\r\n");
    }




    // =====================
    // OPUS ENCODER
    // =====================
    int err;
    OpusEncoder* encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(128000)); // Stereo nên để bitrate cao hơn (128kbps)

    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    // =====================
    // LOOP
    // =====================
    BYTE* data;
    UINT32 frames;
    DWORD flags;

    unsigned char opusData[1275];

    // THÊM: Lấy trình điều khiển Volume của Loa PC
    IAudioEndpointVolume* pEndpointVolume = NULL;
    pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pEndpointVolume);

    // Nếu người dùng chọn "Tắt tiếng loa PC khi gửi"
    if (mutePCState) {
        pEndpointVolume->SetMute(TRUE, NULL);
    }

    std::vector<short> accumulator;
    accumulator.reserve(960 * 2);

    auto lastSend = std::chrono::high_resolution_clock::now();

    while (sending) {

        pCaptureClient->GetNextPacketSize(&frames);

        if (frames > 0) {
            pCaptureClient->GetBuffer(&data, &frames, &flags, NULL, NULL);
            float* fdata = (float*)data;
            int nChannels = pwfx->nChannels;

            // 1. Đưa dữ liệu vào bộ tích lũy (Downmix hoặc Stereo tùy bạn)
            for (UINT32 i = 0; i < frames; i++) {
                short left = (short)(fdata[i * nChannels] * 32767.0f);
                short right = (nChannels >= 2) ? (short)(fdata[i * nChannels + 1] * 32767.0f) : left;

                accumulator.push_back(left);
                accumulator.push_back(right);
            }
            pCaptureClient->ReleaseBuffer(frames);

            // 2. Kiểm tra nếu tích lũy đủ 960 frames (mỗi frame có 2 mẫu cho Stereo)
            // 960 frames * 2 channels = 1920 samples

            size_t readIndex = 0;

            while (accumulator.size() - readIndex >= 1920) {

                short* pcmFrame = &accumulator[readIndex];

                int len = opus_encode(encoder, pcmFrame, 960, opusData, sizeof(opusData));

                if (len > 0) {
                    sendto(sock, (char*)opusData, len, 0, (sockaddr*)&addr, sizeof(addr));
                    //Log(" gui data len=" + std::to_string(len) + "\r\n");
                }

                readIndex += 1920;
            }
            // cleanup tránh RAM phình
            if (readIndex > 0) {
                accumulator.erase(accumulator.begin(), accumulator.begin() + readIndex);
                readIndex = 0;
            }
        } else {
            Sleep(1); // Tránh chiếm dụng 100% CPU khi không có dữ liệu
        }
    }


    // CLEANUP: Mở lại tiếng khi ngừng gửi
    if (mutePCState) {
        pEndpointVolume->SetMute(FALSE, NULL);
    }

    if (pEndpointVolume) pEndpointVolume->Release();


    // =====================
     // CLEANUP (QUAN TRỌNG)
     // =====================
    pAudioClient->Stop();

    // Giải phóng các interface theo thứ tự ngược lại
    if (pCaptureClient) pCaptureClient->Release();
    if (pAudioClient) pAudioClient->Release();
    if (pDevice) pDevice->Release();
    if (pEnumerator) pEnumerator->Release();

    CoUninitialize(); // Kết thúc COM

    opus_encoder_destroy(encoder);
    closesocket(sock);
    WSACleanup();

    isThreadRunning = false;
    Log("⏹ Audio Send Thread Stopped\r\n");

}


void StartSendAudio(const std::string& ip, int port) {
    if (sending) return;

    sending = true;
    sendThread = std::thread(SendLoop, ip, port);
}

void StopSendAudio() {
    sending = false;

    if (sendThread.joinable()) {
        // QUAN TRỌNG: Dùng detach() thay vì join()
        // Thread sẽ tự chạy nốt phần Cleanup ở chạy ngầm
        sendThread.detach();
    }
}

bool IsSendingAudio() {
    return sending;
}

