#include "AutoUpdater.h"
#include "OStreamSink.h"

#include "Windows/Console.h"
#include "Windows/MinimumLatencyAudioClient.h"
#include "Windows/MessagingWindow.h"
#include "Windows/TrayIcon.h"

#include "../res/resource.h"

#include <spdlog/spdlog.h>

#include <conio.h>

#include <locale>
#include <iostream>
#include <sstream>

using namespace miniant::AutoUpdater;
using namespace miniant::Spdlog;
using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

constexpr Version APP_VERSION(0, 2, 0);
constexpr TCHAR COMMAND_LINE_OPTION_TRAY[] = TEXT("--tray");

void WaitForAnyKey(const std::string& message) {
    while (_kbhit()) {
        _getch();
    }

    spdlog::get("app_out")->info(message);
    _getch();
}

void DisplayExitMessage(bool success) {
    if (success) {
        WaitForAnyKey("\nPress any key to disable and exit . . .");
    } else {
        WaitForAnyKey("\nPress any key to exit . . .");
    }
}

std::string ToLower(const std::string& string) {
    std::string result;
    for (const auto& c : string) {
        result.append(1, std::tolower(c, std::locale()));
    }

    return result;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    auto oss = std::make_shared<std::ostringstream>();
    auto sink = std::make_shared<OStreamSink>(oss, true);
    auto app_out = std::make_shared<spdlog::logger>("app_out", sink);
    app_out->set_pattern("%v");
    spdlog::register_logger(app_out);

    auto console = std::make_shared<Console>([=] {
        std::lock_guard<std::mutex> lock(sink->GetMutex());
        std::cout << oss->str();
        std::cout.flush();
        oss->set_rdbuf(std::cout.rdbuf());
        });

    std::unique_ptr<MessagingWindow> window;
    std::unique_ptr<TrayIcon> trayIcon;

    std::wstring commandLine(pCmdLine);
    bool success = true;

    if (commandLine == COMMAND_LINE_OPTION_TRAY) {
        tl::expected windowPtrResult = MessagingWindow::CreatePtr();
        if (!windowPtrResult) {
#pragma push_macro("GetMessage")
#undef GetMessage
            app_out->error("Error: {}", windowPtrResult.error().GetMessage());
            return 1;
        }

        window = std::move(*windowPtrResult);
        HICON hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
        trayIcon = std::make_unique<TrayIcon>(*window, hIcon);
        trayIcon->SetLButtonUpHandler([=, &success](TrayIcon& trayIcon) {
            trayIcon.Hide();
            console->Open();

            DisplayExitMessage(success);

            console->Close();
            return std::optional<LRESULT>();
            });
        trayIcon->Show();
    } else {
        console->Open();
    }

    app_out->info("REAL - REduce Audio Latency {}, mini)(ant, 2018-2019", APP_VERSION.ToString());
    app_out->info("Project: https://github.com/miniant-git/REAL\n");

    auto audioClient = MinimumLatencyAudioClient::Start();
    if (!audioClient) {
        success = false;
        app_out->info("ERROR: Could not enable low-latency mode.\n");
    } else {
        app_out->info("Minimum audio latency enabled on the FIRST playback device!\n");
        auto properties = audioClient->GetProperties();
        if (properties) {
            app_out->info(
                "Device properties:\n    Sample rate{:.>16} Hz\n    Buffer size (min){:.>10} samples ({} ms) [current]\n    Buffer size (max){:.>10} samples ({} ms)\n    Buffer size (default){:.>6} samples ({} ms)\n",
                properties->sampleRate,
                properties->minimumBufferSize, 1000.0f * properties->minimumBufferSize / properties->sampleRate,
                properties->maximumBufferSize, 1000.0f * properties->maximumBufferSize / properties->sampleRate,
                properties->defaultBufferSize, 1000.0f * properties->defaultBufferSize / properties->sampleRate);
        }
    }


#pragma pop_macro("GetMessage")
    if (commandLine == COMMAND_LINE_OPTION_TRAY) {
        MSG msg;
        while (::GetMessage(&msg, NULL, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    } else {
        DisplayExitMessage(success);
    }

    return 0;
}
