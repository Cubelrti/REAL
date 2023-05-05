#include "MinimumLatencyAudioClient.h"
#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>
#include <strsafe.h>
#include <cassert>
#include <iostream>

using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient3 = __uuidof(IAudioClient3);

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

MinimumLatencyAudioClient::MinimumLatencyAudioClient(MinimumLatencyAudioClient&& other) {
    assert(other.m_pAudioClient != nullptr);
    assert(other.m_pFormat != nullptr);

    m_pAudioClient = other.m_pAudioClient;
    m_pFormat = other.m_pFormat;

    other.m_pAudioClient = nullptr;
    other.m_pFormat = nullptr;
}
MinimumLatencyAudioClient::MinimumLatencyAudioClient(void* pAudioClient, void* pFormat) :
    m_pAudioClient(pAudioClient), m_pFormat(pFormat) {}

MinimumLatencyAudioClient::~MinimumLatencyAudioClient() {
    Uninitialise();
}

MinimumLatencyAudioClient& MinimumLatencyAudioClient::operator= (MinimumLatencyAudioClient&& rhs) {
    assert(rhs.m_pAudioClient != nullptr);
    assert(rhs.m_pFormat != nullptr);

    Uninitialise();
    m_pAudioClient = rhs.m_pAudioClient;
    m_pFormat = rhs.m_pFormat;

    rhs.m_pAudioClient = nullptr;
    rhs.m_pFormat = nullptr;

    return *this;
}

void MinimumLatencyAudioClient::Uninitialise() {
    if (m_pAudioClient == nullptr) {
        assert(m_pFormat == nullptr);
        return;
    }

    assert(m_pFormat != nullptr);

    static_cast<IAudioClient3*>(m_pAudioClient)->Release();
    m_pAudioClient = nullptr;

    CoTaskMemFree(m_pFormat);
    m_pFormat = nullptr;
}

tl::expected<MinimumLatencyAudioClient::Properties, WindowsError> MinimumLatencyAudioClient::GetProperties() {
    Properties properties;
    HRESULT hr = static_cast<IAudioClient3*>(m_pAudioClient)->GetSharedModeEnginePeriod(
        static_cast<WAVEFORMATEX*>(m_pFormat),
        &properties.defaultBufferSize,
        &properties.fundamentalBufferSize,
        &properties.minimumBufferSize,
        &properties.maximumBufferSize);
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    properties.sampleRate = static_cast<WAVEFORMATEX*>(m_pFormat)->nSamplesPerSec;
    properties.bitsPerSample = static_cast<WAVEFORMATEX*>(m_pFormat)->wBitsPerSample;
    properties.numChannels = static_cast<WAVEFORMATEX*>(m_pFormat)->nChannels;

    return properties;
}


//
//  Retrieves the device friendly name for a particular device in a device collection.  
//
//  The returned string was allocated using malloc() so it should be freed using free();
//
LPWSTR GetDeviceName(IMMDeviceCollection* DeviceCollection, UINT DeviceIndex)
{
    IMMDevice* device;
    LPWSTR deviceId;
    HRESULT hr;


    hr = DeviceCollection->Item(DeviceIndex, &device);
    if (FAILED(hr))
    {
        printf("Unable to get device %d: %x\n", DeviceIndex, hr);
        return NULL;
    }
    hr = device->GetId(&deviceId);
    if (FAILED(hr))
    {
        printf("Unable to get device %d id: %x\n", DeviceIndex, hr);
        return NULL;
    }

    std::wcout << "deviceID" << deviceId << std::endl;

    IPropertyStore* propertyStore;
    hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
    SafeRelease(&device);
    if (FAILED(hr))
    {
        printf("Unable to open device %d property store: %x\n", DeviceIndex, hr);
        return NULL;
    }

    PROPVARIANT friendlyName;
    PropVariantInit(&friendlyName);
    hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &friendlyName);
    SafeRelease(&propertyStore);

    if (FAILED(hr))
    {
        printf("Unable to retrieve friendly name for device %d : %x\n", DeviceIndex, hr);
        return NULL;
    }

    wchar_t deviceName[128];
    hr = StringCbPrintf(deviceName, sizeof(deviceName), L"%s (%s)", friendlyName.vt != VT_LPWSTR ? L"Unknown" : friendlyName.pwszVal, deviceId);
    if (FAILED(hr))
    {
        printf("Unable to format friendly name for device %d : %x\n", DeviceIndex, hr);
        return NULL;
    }

    PropVariantClear(&friendlyName);
    CoTaskMemFree(deviceId);

    wchar_t* returnValue = _wcsdup(deviceName);
    if (returnValue == NULL)
    {
        printf("Unable to allocate buffer for return\n");
        return NULL;
    }
    return returnValue;
}

tl::expected<MinimumLatencyAudioClient, WindowsError> MinimumLatencyAudioClient::Start() {
    HRESULT hr;

    hr = CoInitialize(NULL);
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    IMMDeviceEnumerator* pEnumerator;
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator,
        NULL,
        CLSCTX_ALL,
        IID_IMMDeviceEnumerator,
        reinterpret_cast<void**>(&pEnumerator));
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    IMMDevice* pDevice;
    IMMDeviceCollection* deviceCollection = NULL;
    hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &deviceCollection);
    UINT deviceCount;
    hr = deviceCollection->GetCount(&deviceCount);
    //for (UINT DeviceIndex = 0; DeviceIndex < deviceCount; DeviceIndex++)
    //{
    //    LPWSTR deviceName = GetDeviceName(deviceCollection, DeviceIndex);
    //    std::cout << DeviceIndex << ": ";
    //    std::wcout << " deviceName : " << deviceName << std::endl;
    //}


    //hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    deviceCollection->Item(0, &pDevice);
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    IAudioClient3* pAudioClient;
    hr = pDevice->Activate(IID_IAudioClient3, CLSCTX_ALL, NULL, reinterpret_cast<void**>(&pAudioClient));
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    WAVEFORMATEX* pFormat;
    hr = pAudioClient->GetMixFormat(&pFormat);
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    UINT32 defaultPeriodInFrames;
    UINT32 fundamentalPeriodInFrames;
    UINT32 minPeriodInFrames;
    UINT32 maxPeriodInFrames;
    hr = pAudioClient->GetSharedModeEnginePeriod(
        pFormat,
        &defaultPeriodInFrames,
        &fundamentalPeriodInFrames,
        &minPeriodInFrames,
        &maxPeriodInFrames);
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    hr = pAudioClient->InitializeSharedAudioStream(
        0,
        minPeriodInFrames,
        pFormat,
        NULL);
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    hr = pAudioClient->Start();
    if (hr != S_OK) {
        return tl::make_unexpected(WindowsError());
    }

    return MinimumLatencyAudioClient(pAudioClient, pFormat);
}
