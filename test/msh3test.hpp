/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_SERVER_SUPPORT 1
#define MSH3_TEST_MODE 1

#include "msh3.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace std;
using namespace std::chrono_literals;

struct TestRequest;

enum TestCleanUpMode {
    CleanUpManual,
    CleanUpAutoDelete,
};

const uint32_t TestTimeout = 250; // milliseconds

template<typename T>
struct TestWaitable {
    T Get() const { return State; }
    void Set(T state) {
        std::lock_guard Lock{Mutex};
        State = state;
        Event.notify_all();
    }
    T Wait() {
        if (!State) {
            std::unique_lock Lock{Mutex};
            Event.wait(Lock, [&]{return State;});
        }
        return State;
    }
    bool WaitFor(uint32_t milliseconds = TestTimeout) {
        if (!State) {
            std::unique_lock Lock{Mutex};
            return Event.wait_for(Lock, milliseconds*1ms, [&]{return State;});
        }
        return true;
    }
private:
    std::mutex Mutex;
    std::condition_variable Event;
    T State { (T)0 };
};

struct TestApi {
    MSH3_API* Handle { MsH3ApiOpen() };
    ~TestApi() noexcept { if (Handle) { MsH3ApiClose(Handle); } }
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_API* () const noexcept { return Handle; }
};

struct TestConnection {
    MSH3_CONNECTION* Handle { nullptr };
    TestWaitable<bool> Connected;
    TestWaitable<bool> ShutdownComplete;
    TestWaitable<TestRequest*> NewRequest;
    TestConnection(
        TestApi& Api,
        const char* ServerName = "localhost",
        uint16_t Port = 4433,
        bool Unsecure = true
        ) noexcept : CleanUp(CleanUpManual) {
        Handle = MsH3ConnectionOpen(Api, &Interface, this, ServerName, Port, Unsecure);
    }
    TestConnection(MSH3_CONNECTION* ServerHandle) noexcept : Handle(ServerHandle), CleanUp(CleanUpAutoDelete) {
        MsH3ConnectionSetCallbackInterface(Handle, &Interface, this);
    }
    ~TestConnection() noexcept { if (Handle) { MsH3ConnectionClose(Handle); } }
    TestConnection(TestConnection& other) = delete;
    TestConnection operator=(TestConnection& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_CONNECTION* () const noexcept { return Handle; }
    MSH3_CONNECTION_STATE GetState(bool WaitForHandshakeComplete = false) const {
        return MsH3ConnectionGetState(Handle, WaitForHandshakeComplete);
    }
    void SetCertificate(MSH3_CERTIFICATE* Certificate) noexcept {
        MsH3ConnectionSetCertificate(Handle, Certificate);
    }
private:
    const TestCleanUpMode CleanUp;
    const MSH3_CONNECTION_IF Interface { s_OnConnected, s_OnShutdownComplete, s_OnNewRequest };
    void OnConnected() noexcept {
        Connected.Set(true);
    }
    void OnShutdownComplete() noexcept {
        ShutdownComplete.Set(true);
        if (CleanUp == CleanUpAutoDelete) {
            delete this;
        }
    }
    void OnNewRequest(MSH3_REQUEST* Request) noexcept;
private: // Static stuff
    static void MSH3_CALL s_OnConnected(MSH3_CONNECTION* /*Connection*/, void* IfContext) noexcept {
        ((TestConnection*)IfContext)->OnConnected();
    }
    static void MSH3_CALL s_OnShutdownComplete(MSH3_CONNECTION* /*Connection*/, void* IfContext) noexcept {
        ((TestConnection*)IfContext)->OnShutdownComplete();
    }
    static void MSH3_CALL s_OnNewRequest(MSH3_CONNECTION* /*Connection*/, void* IfContext, MSH3_REQUEST* Request) noexcept {
        ((TestConnection*)IfContext)->OnNewRequest(Request);
    }
};

struct TestRequest {
    MSH3_REQUEST* Handle { nullptr };
    TestWaitable<bool> Complete;
    TestWaitable<bool> ShutdownComplete;
    bool Aborted {false};
    uint64_t AbortError {0};
    TestRequest(
        TestConnection& Connection,
        const MSH3_HEADER* Headers,
        size_t HeadersCount,
        MSH3_REQUEST_FLAGS Flags = MSH3_REQUEST_FLAG_NONE
        ) noexcept : CleanUp(CleanUpManual) {
        Handle = MsH3RequestOpen(Connection, &Interface, this, Headers, HeadersCount, Flags);
    }
    TestRequest(MSH3_REQUEST* ServerHandle) noexcept : Handle(ServerHandle), CleanUp(CleanUpAutoDelete) {
        MsH3RequestSetCallbackInterface(Handle, &Interface, this);
    }
    ~TestRequest() noexcept { if (Handle) { MsH3RequestClose(Handle); } }
    TestRequest(TestRequest& other) = delete;
    TestRequest operator=(TestRequest& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_REQUEST* () const noexcept { return Handle; }
    bool Send(
        MSH3_REQUEST_FLAGS Flags,
        const void* Data,
        uint32_t DataLength,
        void* AppContext = nullptr
        ) noexcept {
        return MsH3RequestSend(Handle, Flags, Data, DataLength, AppContext);
    }
    void Shutdown(
        MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
        uint64_t _AbortError = 0
        ) noexcept {
        return MsH3RequestShutdown(Handle, Flags, _AbortError);
    }
    bool SendHeaders(
        const MSH3_HEADER* Headers,
        size_t HeadersCount,
        MSH3_REQUEST_FLAGS Flags
        ) noexcept {
        return MsH3RequestSendHeaders(Handle, Headers, HeadersCount, Flags);
    }
private:
    const TestCleanUpMode CleanUp;
    const MSH3_REQUEST_IF Interface { s_OnHeaderReceived, s_OnDataReceived, s_OnComplete, s_OnShutdownComplete, s_OnDataSent };
    void OnHeaderReceived(const MSH3_HEADER* /*Header*/) noexcept {
    }
    void OnDataReceived(uint32_t /*Length*/, const uint8_t* /*Data*/) noexcept {
    }
    void OnComplete(bool _Aborted, uint64_t _AbortError) noexcept {
        Aborted = _Aborted;
        AbortError = _AbortError;
        Complete.Set(true);
    }
    void OnShutdownComplete() noexcept {
        ShutdownComplete.Set(true);
        if (CleanUp == CleanUpAutoDelete) {
            delete this;
        }
    }
    void OnDataSent(void* /*SendContext*/) noexcept {
    }
private: // Static stuff
    static void MSH3_CALL s_OnHeaderReceived(MSH3_REQUEST* /*Request*/, void* IfContext, const MSH3_HEADER* Header) noexcept {
        ((TestRequest*)IfContext)->OnHeaderReceived(Header);
    }
    static void MSH3_CALL s_OnDataReceived(MSH3_REQUEST* /*Request*/, void* IfContext, uint32_t Length, const uint8_t* Data) noexcept {
        ((TestRequest*)IfContext)->OnDataReceived(Length, Data);
    }
    static void MSH3_CALL s_OnComplete(MSH3_REQUEST* /*Request*/, void* IfContext, bool Aborted, uint64_t AbortError) noexcept {
        ((TestRequest*)IfContext)->OnComplete(Aborted, AbortError);
    }
    static void MSH3_CALL s_OnShutdownComplete(MSH3_REQUEST* /*Request*/, void* IfContext) noexcept {
        ((TestRequest*)IfContext)->OnShutdownComplete();
    }
    static void MSH3_CALL s_OnDataSent(MSH3_REQUEST* /*Request*/, void* IfContext, void* SendContext) noexcept {
        ((TestRequest*)IfContext)->OnDataSent(SendContext);
    }
};

void TestConnection::OnNewRequest(MSH3_REQUEST* Request) noexcept {
    NewRequest.Set(new(std::nothrow) TestRequest(Request));
}

struct TestCertificate {
    MSH3_CERTIFICATE* Handle { nullptr };
    TestCertificate(TestApi& Api) { // Self-signed certificate
        const MSH3_CERTIFICATE_CONFIG Config = { MSH3_CERTIFICATE_TYPE_SELF_SIGNED };
        Handle = MsH3CertificateOpen(Api, &Config);
    }
    TestCertificate(TestApi& Api, const MSH3_CERTIFICATE_CONFIG& Config) {
        Handle = MsH3CertificateOpen(Api, &Config);
    }
    ~TestCertificate() noexcept { if (Handle) { MsH3CertificateClose(Handle); } }
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_CERTIFICATE* () const noexcept { return Handle; }
};

struct TestAddr {
    MSH3_ADDR Addr {0};
    TestAddr(uint16_t Port = 4433) {
#if _WIN32
        Addr.Ipv4.sin_port = _byteswap_ushort(Port);
#else
        Addr.Ipv4.sin_port = __builtin_bswap16(Port);
#endif
    }
    operator const MSH3_ADDR* () const noexcept { return &Addr; }
};

struct TestListener {
    MSH3_LISTENER* Handle { nullptr };
    const MSH3_LISTENER_IF Interface { s_OnNewConnection };
    TestWaitable<TestConnection*> NewConnection;
    TestListener(TestApi& Api, const MSH3_ADDR* Address = TestAddr()) noexcept {
        Handle = MsH3ListenerOpen(Api, Address, &Interface, this);
    }
    ~TestListener() noexcept { if (Handle) { MsH3ListenerClose(Handle); } }
    TestListener(TestListener& other) = delete;
    TestListener operator=(TestListener& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_LISTENER* () const noexcept { return Handle; }
private:
    void OnNewConnection(MSH3_CONNECTION* Connection, const char* /*ServerName*/, uint16_t /*ServerNameLength*/) noexcept {
        NewConnection.Set(new(std::nothrow) TestConnection(Connection));
    }
private: // Static stuff
    static void MSH3_CALL s_OnNewConnection(MSH3_LISTENER* /*Listener*/, void* IfContext, MSH3_CONNECTION* Connection, const char* ServerName, uint16_t ServerNameLength) noexcept {
        ((TestListener*)IfContext)->OnNewConnection(Connection, ServerName, ServerNameLength);
    }
};
