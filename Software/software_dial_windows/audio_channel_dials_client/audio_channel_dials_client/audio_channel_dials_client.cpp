
#include <windows.h>
#include <audiopolicy.h>
#include <commctrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <sstream>
#include <fstream>
#include <atlstr.h>

struct Weight{
    std::string name;
    int value;
};

struct Config {
    int MAX_VOL;
    int NUM_SESSION_DIALS;
    std::string port;
};

void enumerate_and_select_audio();

static std::vector<Weight> weights{
    {"ERROR",-100}
};

void EXIT_ON_ERROR(HRESULT hr) {
    if (hr != S_OK)
        exit(1);
}

void EXIT_SERIAL_ERROR(BOOL error) {
    if (error != TRUE)
        exit(1);
}

class CAudioSessionEvents : public IAudioSessionEvents {
    LONG _cRef;

public:
    CAudioSessionEvents() : _cRef(1) {}

    ~CAudioSessionEvents() {}

    // IUnknown methods -- AddRef, Release, and QueryInterface

    ULONG STDMETHODCALLTYPE AddRef() {
        return InterlockedIncrement(&_cRef);
    }

    ULONG STDMETHODCALLTYPE Release() {
        ULONG ulRef = InterlockedDecrement(&_cRef);
        if (0 == ulRef) {
            delete this;
        }
        return ulRef;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID  riid, VOID** ppvInterface) {
        if (IID_IUnknown == riid) {
            AddRef();
            *ppvInterface = (IUnknown*)this;
        }
        else if (__uuidof(IAudioSessionEvents) == riid) {
            AddRef();
            *ppvInterface = (IAudioSessionEvents*)this;
        }
        else {
            *ppvInterface = NULL;
            return E_NOINTERFACE;
        }
        return S_OK;
    }

    // Notification methods for audio session events

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) {
        if (NewMute) {
            //printf("MUTE\n");
        }
        else {
            //printf("Volume = %d percent\n",(UINT32)(100 * NewVolume + 0.5));
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD ChannelCount, float NewChannelVolumeArray[], DWORD ChangedChannel, LPCGUID EventContext) {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState NewState) {
        std::string pszState = "?????";

        switch (NewState)
        {
        case AudioSessionStateActive:
            pszState = "active";
            break;
        case AudioSessionStateInactive:
            pszState = "inactive";
            break;
        }
        //printf("New session state = %s\n", pszState);

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) {
        std::string pszReason = "?????";

        switch (DisconnectReason) {
        case DisconnectReasonDeviceRemoval:
            pszReason = "device removed";
            break;
        case DisconnectReasonServerShutdown:
            pszReason = "server shut down";
            break;
        case DisconnectReasonFormatChanged:
            pszReason = "format changed";
            break;
        case DisconnectReasonSessionLogoff:
            pszReason = "user logged off";
            break;
        case DisconnectReasonSessionDisconnected:
            pszReason = "session disconnected";
            break;
        case DisconnectReasonExclusiveModeOverride:
            pszReason = "exclusive-mode override";
            break;
        }
        enumerate_and_select_audio();
        //printf("Audio session disconnected (reason: %s)\n", pszReason);

        return S_OK;
    }
};

class CSessionNotifications : public IAudioSessionNotification {
private:

    LONG m_cRefAll;
    HWND m_hwndMain;

    ~CSessionNotifications() {};

public:


    CSessionNotifications(HWND hWnd) :
        m_cRefAll(1),
        m_hwndMain(hWnd)

    {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
    {
        if (IID_IUnknown == riid)
        {
            AddRef();
            //*ppvInterface = (IUnknown*)this;
        }
        else if (__uuidof(IAudioSessionNotification) == riid)
        {
            AddRef();
            //*ppvInterface = (IAudioSessionNotification*)this;
        }
        else
        {
            //*ppvInterface = NULL;
            return E_NOINTERFACE;
        }
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        return InterlockedIncrement(&m_cRefAll);
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        ULONG ulRef = InterlockedDecrement(&m_cRefAll);
        if (0 == ulRef)
        {
            delete this;
        }
        return ulRef;
    }

    HRESULT OnSessionCreated(IAudioSessionControl* pNewSession)
    {
        if (pNewSession)
        {
            //PostMessage(m_hwndMain, WM_SESSION_CREATED, 0, 0);
            enumerate_and_select_audio();
            std::cout << "New Session" << std::endl;
        }
        return S_OK;
    }
};


struct AudioStreamInterfaces {
    ISimpleAudioVolume* simple_audio_volume;
    IAudioSessionControl* audio_session_control;
    IAudioSessionControl2* audio_session_control2;
    CAudioSessionEvents* audio_session_events;
    Weight name_weight;
    ~AudioStreamInterfaces() {
        //delete simple_audio_volume;
        //delete audio_session_control;
        //delete audio_session_control2;
        //delete audio_session_events;
    }
};

int num_controlled_sessions;
Config config{ 100,4,"" };
HWND g_hDlg = NULL;
GUID g_guidMyContext = GUID_NULL;
IAudioSessionManager2* audio_seesion_manager2 = NULL;
IAudioEndpointVolume* audio_endpoint_volume = NULL;
int num_audio_sessions;
static std::vector<AudioStreamInterfaces> audio_streams_interfaces(config.NUM_SESSION_DIALS);
static CSessionNotifications* audio_session_events;

std::string ProcessIdToName(DWORD processId){
    std::string ret = "ERROR\n";
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId); /* This is the PID, you can find one from windows task manager */
    
    if (handle){
        DWORD buffSize = 1024;
        CHAR buffer[1024];
        if (QueryFullProcessImageNameA(handle, 0, buffer, &buffSize)){
            ret = buffer;
        }else{
            printf("Error GetModuleBaseNameA : %lu", GetLastError());
        }
        CloseHandle(handle);
    }else{
        printf("Error OpenProcess : %lu", GetLastError());
    }
    return ret;
}

int initSerial(HANDLE* serial_handle, LPCWSTR filename) {
    *serial_handle = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);

    if (*serial_handle == INVALID_HANDLE_VALUE)
        printf("Error in opening serial port\n");
    else
        printf("Opening serial port successful\n");

    DCB serialParams = { 0 };
    serialParams.DCBlength = sizeof(serialParams);
    GetCommState(*serial_handle, &serialParams);
    std::cout << (int)serialParams.BaudRate << " " << (int)serialParams.ByteSize << " " << (int)serialParams.StopBits << " " << (int)serialParams.Parity << "\n";

    serialParams.BaudRate = CBR_9600;
    serialParams.ByteSize = 8;
    serialParams.StopBits = 1;
    serialParams.Parity = 0;
    EXIT_SERIAL_ERROR(SetCommState(*serial_handle, &serialParams));

    COMMTIMEOUTS timeout = { 0 };
    timeout.ReadIntervalTimeout = 3;
    timeout.ReadTotalTimeoutConstant = 3;
    timeout.ReadTotalTimeoutMultiplier = 3;
    timeout.WriteTotalTimeoutConstant = 3;
    timeout.WriteTotalTimeoutMultiplier = 1;
    EXIT_SERIAL_ERROR(SetCommTimeouts(*serial_handle, &timeout));
    EXIT_SERIAL_ERROR(SetCommMask(*serial_handle, EV_RXCHAR | EV_ERR));
    return 0;
}

void selectAudioSessions(IAudioSessionEnumerator* audio_session_enumerator, int num_audio_sessions) {
    num_controlled_sessions = num_audio_sessions;
    std::cout << "Number of sessions: "<< num_controlled_sessions << "\n";

    std::vector<AudioStreamInterfaces> all_audio_streams_interfaces = std::vector<AudioStreamInterfaces>();
    int num_audio_sessions_added = 0;

    for (int i = 0; i < num_controlled_sessions; i++) {
        AudioStreamInterfaces audio_streams_interfaces = AudioStreamInterfaces();
        EXIT_ON_ERROR(audio_session_enumerator->GetSession(i, &audio_streams_interfaces.audio_session_control));
        
        EXIT_ON_ERROR(audio_streams_interfaces.audio_session_control->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&audio_streams_interfaces.simple_audio_volume));
        EXIT_ON_ERROR(audio_streams_interfaces.audio_session_control->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&audio_streams_interfaces.audio_session_control2));

        DWORD p_id;
        audio_streams_interfaces.audio_session_control2->GetProcessId(&p_id);
        audio_streams_interfaces.name_weight = Weight();
        audio_streams_interfaces.name_weight.name = ProcessIdToName(p_id);
        audio_streams_interfaces.name_weight.value = 0;

        for (auto const& w : weights){
            if (audio_streams_interfaces.name_weight.name.find(w.name) != std::string::npos) {
                audio_streams_interfaces.name_weight.value += w.value;
            }
        }

        if (audio_streams_interfaces.name_weight.value <= -100)
            continue;

        num_audio_sessions_added++;
        std::cout << audio_streams_interfaces.name_weight.name << std::endl;
        all_audio_streams_interfaces.push_back(audio_streams_interfaces);
    }


    auto sortRuleLambda = [](AudioStreamInterfaces const& s1, AudioStreamInterfaces const& s2) -> bool{
        return s1.name_weight.value > s2.name_weight.value;
    };

    std::sort(all_audio_streams_interfaces.begin(), all_audio_streams_interfaces.end(), sortRuleLambda);
    
    if (num_audio_sessions_added > config.NUM_SESSION_DIALS)
        num_audio_sessions_added = config.NUM_SESSION_DIALS;

    audio_streams_interfaces = std::vector<AudioStreamInterfaces>(config.NUM_SESSION_DIALS);
    for (int i = 0; i < num_audio_sessions_added; i++) {
        audio_streams_interfaces[i] = all_audio_streams_interfaces[i];
        audio_streams_interfaces[i].audio_session_events = new CAudioSessionEvents();
        audio_streams_interfaces[i].audio_session_control->RegisterAudioSessionNotification(audio_streams_interfaces[i].audio_session_events);
    }
    num_controlled_sessions = num_audio_sessions_added;
}

void enumerate_and_select_audio() {
    IAudioSessionEnumerator* audio_session_enumerator = NULL;
    EXIT_ON_ERROR(audio_seesion_manager2->GetSessionEnumerator(&audio_session_enumerator));
    EXIT_ON_ERROR(audio_session_enumerator->GetCount(&num_audio_sessions));
    selectAudioSessions(audio_session_enumerator, num_audio_sessions);
}

int initAudio() {
   
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    CoInitialize(NULL);

    EXIT_ON_ERROR(CoCreateGuid(&g_guidMyContext));
    EXIT_ON_ERROR(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator));
    EXIT_ON_ERROR(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice));
    EXIT_ON_ERROR(pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&audio_seesion_manager2));
    EXIT_ON_ERROR(pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&audio_endpoint_volume));

    audio_session_events = new CSessionNotifications(g_hDlg);
    audio_seesion_manager2->RegisterSessionNotification(audio_session_events);
    enumerate_and_select_audio();
    return 0;
}

void parse_command(std::string command) {

    int index = std::stoi(command.substr(2, 1));
    if (index > num_controlled_sessions) {
        return;
    }
    std::string sub_command = command.substr(6);
    index--;

    if (index == -1) {
        if (sub_command == std::string("11")) {
            float volume;
            audio_endpoint_volume->GetMasterVolumeLevelScalar(&volume);
            volume += 0.1f;
            if (volume > 1.0f)
                volume = 1.0f;
            audio_endpoint_volume->SetMasterVolumeLevelScalar(volume, &g_guidMyContext);
        }
        else if (sub_command == std::string("10")) {
            float volume;
            audio_endpoint_volume->GetMasterVolumeLevelScalar(&volume);
            volume -= 0.1f;
            if (volume < 0.0f)
                volume = 0.0f;
            audio_endpoint_volume->SetMasterVolumeLevelScalar(volume, &g_guidMyContext);
        }

    }
    //else if (sub_command == std::string("00")) {
    //    std::cout << "down\n";
    //}
    else if (sub_command == std::string("01")) {
        BOOL is_muted;
        audio_streams_interfaces[index].simple_audio_volume->GetMute(&is_muted);
        audio_streams_interfaces[index].simple_audio_volume->SetMute(!is_muted, &g_guidMyContext);
    }
    else if (sub_command == std::string("11")) {
        float volume;
        audio_streams_interfaces[index].simple_audio_volume->GetMasterVolume(&volume);
        volume += 0.1f;
        if (volume > 1.0f)
            volume = 1.0f;
        audio_streams_interfaces[index].simple_audio_volume->SetMasterVolume(volume, &g_guidMyContext);
    }
    else if (sub_command == std::string("10")) {
        float volume;
        audio_streams_interfaces[index].simple_audio_volume->GetMasterVolume(&volume);
        volume -= 0.1f;
        if (volume < 0.0f)
            volume = 0.0f;
        audio_streams_interfaces[index].simple_audio_volume->SetMasterVolume(volume, &g_guidMyContext);
    }

}

void store_line(std::string key, std::string value) {
    if (key.compare("port") == 0) {
        config.port = value;
        
    }
    else if (key.length() >= 6 && key.substr(0,4).compare("prg:") == 0) {
        std::cout << key.substr(4) << " " << value << std::endl;
        weights.push_back({ key.substr(4), std::stoi(value) });
    }
}

bool load_config(std::string filename) {
    std::ifstream input;
    input.open(filename);
    if( !input) {
        std::cout << "Can't open file " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(input, line))
    {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '='))
        {
            std::string value;
            if (std::getline(is_line, value))
                store_line(key, value);
        }
    }
    return true;
}

int main()
{
    if (!load_config("C:/Users/idwcorni/Documents/audio_dials.conf"))
        return EXIT_FAILURE;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    initAudio();
    std::cout << std::endl;
    HANDLE serial_handle;
    std::cout << config.port.c_str() << std::endl;
    initSerial(&serial_handle, CStringW(config.port.c_str()));

    DWORD dwEventMask;
    char temp_char;
    char SerialBuffer[256];
    char command[9];
    DWORD numBytesRead;

    command[8] = '\0';
    while (true) {

        EXIT_SERIAL_ERROR(WaitCommEvent(serial_handle, &dwEventMask, NULL));
        ReadFile(serial_handle, &command, sizeof(command) - 1, &numBytesRead, NULL);

        do {
            ReadFile(serial_handle, &temp_char, sizeof(temp_char), &numBytesRead, NULL);
        } while (numBytesRead > 0);

        /*for (int i = 0; i < 8; i++)
            std::cout << command[i];
        std::cout << "\n";*/
        parse_command(std::string(command));
    }
    CloseHandle(serial_handle);//Closing the Serial Port
    return EXIT_SUCCESS;
}

void release() {
    audio_seesion_manager2->Release();
}