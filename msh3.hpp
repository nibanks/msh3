/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace std;
using namespace std::chrono_literals;

#if MSH3_TEST_MODE
#define TEST_DEF(x) = x
#else
#define TEST_DEF(x)
#endif

struct MsH3Request;

enum MsH3CleanUpMode {
    CleanUpManual,
    CleanUpAutoDelete,
};

template<typename T>
struct MsH3Waitable {
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
    bool WaitFor(uint32_t milliseconds TEST_DEF(250)) {
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

struct MsH3Api {
    MSH3_API* Handle { MsH3ApiOpen() };
    ~MsH3Api() noexcept { if (Handle) { MsH3ApiClose(Handle); } }
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_API* () const noexcept { return Handle; }
};

struct MsH3Addr {
    MSH3_ADDR Addr {0};
#if MSH3_TEST_MODE
    MsH3Addr(uint16_t Port = 4433) {
#else
    MsH3Addr(uint16_t Port = 0) {
#endif
        SetPort(Port);
    }
    operator const MSH3_ADDR* () const noexcept { return &Addr; }
    void SetPort(uint16_t Port) noexcept { MSH3_SET_PORT(&Addr, Port); }
};

struct MsH3Connection {
    MSH3_CONNECTION* Handle { nullptr };
    MsH3Waitable<bool> Connected;
    MsH3Waitable<bool> ShutdownComplete;
    MsH3Waitable<MsH3Request*> NewRequest;
    MsH3Connection(
        MsH3Api& Api,
        const char* ServerName TEST_DEF("localhost"),
        const MsH3Addr& ServerAddress = MsH3Addr(),
#if MSH3_TEST_MODE
        bool Unsecure = true
#else
        bool Unsecure = false
#endif
        ) noexcept : CleanUp(CleanUpManual) {
        Handle = MsH3ConnectionOpen(Api, &Interface, this, ServerName, ServerAddress, Unsecure);
    }
#ifdef MSH3_SERVER_SUPPORT
    MsH3Connection(MSH3_CONNECTION* ServerHandle) noexcept : Handle(ServerHandle), CleanUp(CleanUpAutoDelete) {
        MsH3ConnectionSetCallbackInterface(Handle, &Interface, this);
    }
#endif
    ~MsH3Connection() noexcept { if (Handle) { MsH3ConnectionClose(Handle); } }
    MsH3Connection(MsH3Connection& other) = delete;
    MsH3Connection operator=(MsH3Connection& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_CONNECTION* () const noexcept { return Handle; }
    void Shutdown(uint64_t ErrorCode = 0) noexcept {
        MsH3ConnectionShutdown(Handle, ErrorCode);
    }
#ifdef MSH3_SERVER_SUPPORT
    void SetCertificate(MSH3_CERTIFICATE* Certificate) noexcept {
        MsH3ConnectionSetCertificate(Handle, Certificate);
    }
#endif
private:
    const MsH3CleanUpMode CleanUp;
    const MSH3_CONNECTION_IF Interface { s_OnConnected, s_OnShutdownByPeer, s_OnShutdownByTransport, s_OnShutdownComplete, s_OnNewRequest };
    void OnConnected() noexcept {
        Connected.Set(true);
    }
    void OnShutdownByPeer(uint64_t /*ErrorCode*/) noexcept {
    }
    void OnShutdownByTransport(MSH3_STATUS /*Status*/) noexcept {
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
        ((MsH3Connection*)IfContext)->OnConnected();
    }
    static void MSH3_CALL s_OnShutdownByPeer(MSH3_CONNECTION* /*Connection*/, void* IfContext, uint64_t ErrorCode) noexcept {
        ((MsH3Connection*)IfContext)->OnShutdownByPeer(ErrorCode);
    }
    static void MSH3_CALL s_OnShutdownByTransport(MSH3_CONNECTION* /*Connection*/, void* IfContext, MSH3_STATUS Status) noexcept {
        ((MsH3Connection*)IfContext)->OnShutdownByTransport(Status);
    }
    static void MSH3_CALL s_OnShutdownComplete(MSH3_CONNECTION* /*Connection*/, void* IfContext) noexcept {
        ((MsH3Connection*)IfContext)->OnShutdownComplete();
    }
    static void MSH3_CALL s_OnNewRequest(MSH3_CONNECTION* /*Connection*/, void* IfContext, MSH3_REQUEST* Request) noexcept {
        ((MsH3Connection*)IfContext)->OnNewRequest(Request);
    }
};

typedef void MSH3_CALL MsH3RequestHeaderRecvCallback(struct MsH3Request* Request, const MSH3_HEADER* Header);
typedef bool MSH3_CALL MsH3RequestDataRecvCallback(struct MsH3Request* Request, uint32_t* Length, const uint8_t* Data);
typedef void MSH3_CALL MsH3RequestComplete(struct MsH3Request* Request, bool Aborted, uint64_t AbortError);

struct MsH3Request {
    MSH3_REQUEST* Handle { nullptr };
    MsH3Waitable<bool> Complete;
    MsH3Waitable<bool> ShutdownComplete;
    bool Aborted {false};
    uint64_t AbortError {0};
    void* AppContext {nullptr};
    MsH3RequestHeaderRecvCallback* HeaderRecvFn {nullptr};
    MsH3RequestDataRecvCallback* DataRecvFn {nullptr};
    MsH3RequestComplete* CompleteFn {nullptr};
    MsH3Request(
        MsH3Connection& Connection,
        const MSH3_HEADER* Headers,
        size_t HeadersCount,
        MSH3_REQUEST_FLAGS Flags = MSH3_REQUEST_FLAG_NONE,
        void* AppContext = nullptr,
        MsH3RequestHeaderRecvCallback* HeaderRecv = nullptr,
        MsH3RequestDataRecvCallback* DataRecv = nullptr,
        MsH3RequestComplete* Complete = nullptr,
        MsH3CleanUpMode CleanUpMode = CleanUpManual
        ) noexcept : AppContext(AppContext), HeaderRecvFn(HeaderRecv), DataRecvFn(DataRecv), CompleteFn(Complete), CleanUp(CleanUpMode) {
        Handle = MsH3RequestOpen(Connection, &Interface, this, Headers, HeadersCount, Flags);
    }
#ifdef MSH3_SERVER_SUPPORT
    MsH3Request(MSH3_REQUEST* ServerHandle) noexcept : Handle(ServerHandle), CleanUp(CleanUpAutoDelete) {
        MsH3RequestSetCallbackInterface(Handle, &Interface, this);
    }
#endif
    ~MsH3Request() noexcept { if (Handle) { MsH3RequestClose(Handle); } }
    MsH3Request(MsH3Request& other) = delete;
    MsH3Request operator=(MsH3Request& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_REQUEST* () const noexcept { return Handle; }
    void CompleteReceive(uint32_t Length) noexcept {
        MsH3RequestCompleteReceive(Handle, Length);
    };
    void SetReceiveEnabled(bool Enabled) noexcept {
        MsH3RequestSetReceiveEnabled(Handle, Enabled);
    };
    bool Send(
        MSH3_REQUEST_FLAGS Flags,
        const void* Data,
        uint32_t DataLength,
        void* SendContext = nullptr
        ) noexcept {
        return MsH3RequestSend(Handle, Flags, Data, DataLength, SendContext);
    }
    void Shutdown(
        MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
        uint64_t _AbortError = 0
        ) noexcept {
        return MsH3RequestShutdown(Handle, Flags, _AbortError);
    }
#ifdef MSH3_SERVER_SUPPORT
    bool SendHeaders(
        const MSH3_HEADER* Headers,
        size_t HeadersCount,
        MSH3_REQUEST_FLAGS Flags
        ) noexcept {
        return MsH3RequestSendHeaders(Handle, Headers, HeadersCount, Flags);
    }
#endif
private:
    const MsH3CleanUpMode CleanUp;
    const MSH3_REQUEST_IF Interface { s_OnHeaderReceived, s_OnDataReceived, s_OnComplete, s_OnShutdownComplete, s_OnDataSent };
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
        if (((MsH3Request*)IfContext)->HeaderRecvFn) {
            ((MsH3Request*)IfContext)->HeaderRecvFn((MsH3Request*)IfContext, Header);
        }
    }
    static bool MSH3_CALL s_OnDataReceived(MSH3_REQUEST* /*Request*/, void* IfContext, uint32_t* Length, const uint8_t* Data) noexcept {
        if (((MsH3Request*)IfContext)->DataRecvFn) {
            return ((MsH3Request*)IfContext)->DataRecvFn((MsH3Request*)IfContext, Length, Data);
        } else {
            return true;
        }
    }
    static void MSH3_CALL s_OnComplete(MSH3_REQUEST* /*Request*/, void* IfContext, bool Aborted, uint64_t AbortError) noexcept {
        if (((MsH3Request*)IfContext)->CompleteFn) {
            ((MsH3Request*)IfContext)->CompleteFn((MsH3Request*)IfContext, Aborted, AbortError);
        }
        ((MsH3Request*)IfContext)->OnComplete(Aborted, AbortError);
    }
    static void MSH3_CALL s_OnShutdownComplete(MSH3_REQUEST* /*Request*/, void* IfContext) noexcept {
        ((MsH3Request*)IfContext)->OnShutdownComplete();
    }
    static void MSH3_CALL s_OnDataSent(MSH3_REQUEST* /*Request*/, void* IfContext, void* SendContext) noexcept {
        ((MsH3Request*)IfContext)->OnDataSent(SendContext);
    }
};

void MsH3Connection::OnNewRequest(MSH3_REQUEST* Request) noexcept {
#if MSH3_SERVER_SUPPORT
    NewRequest.Set(new(std::nothrow) MsH3Request(Request));
#else
    UNREFERENCED_PARAMETER(Request);
#endif
}

#if MSH3_SERVER_SUPPORT

struct MsH3Certificate {
    MSH3_CERTIFICATE* Handle { nullptr };
#if MSH3_TEST_MODE
    MsH3Certificate(MsH3Api& Api) { // Self-signed certificate
        const MSH3_CERTIFICATE_CONFIG Config = { MSH3_CERTIFICATE_TYPE_SELF_SIGNED };
        Handle = MsH3CertificateOpen(Api, &Config);
    }
#endif
    MsH3Certificate(MsH3Api& Api, const MSH3_CERTIFICATE_CONFIG& Config) {
        Handle = MsH3CertificateOpen(Api, &Config);
    }
    ~MsH3Certificate() noexcept { if (Handle) { MsH3CertificateClose(Handle); } }
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_CERTIFICATE* () const noexcept { return Handle; }
};

struct MsH3Listener {
    MSH3_LISTENER* Handle { nullptr };
    const MSH3_LISTENER_IF Interface { s_OnNewConnection };
    MsH3Waitable<MsH3Connection*> NewConnection;
    MsH3Listener(MsH3Api& Api, const MsH3Addr& Address TEST_DEF(MsH3Addr())) noexcept {
        Handle = MsH3ListenerOpen(Api, Address, &Interface, this);
    }
    ~MsH3Listener() noexcept { if (Handle) { MsH3ListenerClose(Handle); } }
    MsH3Listener(MsH3Listener& other) = delete;
    MsH3Listener operator=(MsH3Listener& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_LISTENER* () const noexcept { return Handle; }
private:
    void OnNewConnection(MSH3_CONNECTION* Connection, const char* /*ServerName*/, uint16_t /*ServerNameLength*/) noexcept {
        NewConnection.Set(new(std::nothrow) MsH3Connection(Connection));
    }
private: // Static stuff
    static void MSH3_CALL s_OnNewConnection(MSH3_LISTENER* /*Listener*/, void* IfContext, MSH3_CONNECTION* Connection, const char* ServerName, uint16_t ServerNameLength) noexcept {
        ((MsH3Listener*)IfContext)->OnNewConnection(Connection, ServerName, ServerNameLength);
    }
};

#endif // MSH3_SERVER_SUPPORT
