/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#pragma once

#include "msh3.h"
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <atomic>

using namespace std;
using namespace std::chrono_literals;

#if MSH3_TEST_MODE
#define TEST_DEF(x) = x
#else
#define TEST_DEF(x)
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

struct MsH3Request;

enum MsH3CleanUpMode {
    CleanUpManual,
    CleanUpAutoDelete,
};

template<typename T>
struct MsH3Waitable {
    T Get() const { return State; }
    T GetAndReset() {
        std::lock_guard<std::mutex> Lock{Mutex};
        auto StateCopy = State;
        State = (T)0;
        return StateCopy;
    }
    void Set(T state) {
        std::lock_guard<std::mutex> Lock{Mutex};
        State = state;
        Event.notify_all();
    }
    T Wait() {
        if (!State) {
            std::unique_lock<std::mutex> Lock{Mutex};
            Event.wait(Lock, [&]{return State;});
        }
        return State;
    }
    bool WaitFor(uint32_t milliseconds TEST_DEF(250)) {
        if (!State) {
            std::unique_lock<std::mutex> Lock{Mutex};
            return Event.wait_for(Lock, milliseconds*1ms, [&]{return State;});
        }
        return true;
    }
private:
    std::mutex Mutex;
    std::condition_variable Event;
    T State { (T)0 };
};

struct MsH3EventQueue {
#if _WIN32
    HANDLE IOCP;
    operator MSH3_EVENTQ* () noexcept { return &IOCP; }
    MsH3EventQueue() : IOCP(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1)) { }
    ~MsH3EventQueue() { CloseHandle(IOCP); }
    bool IsValid() const noexcept { return IOCP != nullptr; }
    bool Enqueue(
        _In_ LPOVERLAPPED lpOverlapped,
        _In_ uint32_t dwNumberOfBytesTransferred = 0,
        _In_ ULONG_PTR dwCompletionKey = 0
        ) noexcept {
        return PostQueuedCompletionStatus(IOCP, dwNumberOfBytesTransferred, dwCompletionKey, lpOverlapped);
    }
    bool Dequeue(
        _Out_writes_to_(ulCount,*ulNumEntriesRemoved) MSH3_CQE* lpCompletionPortEntries,
        _In_ uint32_t ulCount,
        _Out_ uint32_t* ulNumEntriesRemoved,
        _In_ uint32_t dwMilliseconds
        ) noexcept {
        return GetQueuedCompletionStatusEx(IOCP, lpCompletionPortEntries, ulCount, (ULONG*)ulNumEntriesRemoved, dwMilliseconds, FALSE);
    }
    static MSH3_SQE* GetSqe(MSH3_CQE* Cqe) noexcept {
        return CONTAINING_RECORD(Cqe->lpOverlapped, MSH3_SQE, Overlapped);
    }
#elif __linux__
    // Linux-specific implementation
    int EpollFd;
    operator MSH3_EVENTQ* () noexcept { return &EpollFd; }
    MsH3EventQueue() : EpollFd(epoll_create1(0)) { }
    ~MsH3EventQueue() { close(EpollFd); }
    bool IsValid() const noexcept { return EpollFd >= 0; }
    bool Enqueue(
        MSH3_SQE* Sqe
        ) noexcept {
        return eventfd_write(Sqe->fd, 1) == 0;
    }
    bool Dequeue(
        MSH3_CQE* lpCompletionPortEntries,
        uint32_t ulCount,
        uint32_t* ulNumEntriesRemoved,
        uint32_t dwMilliseconds
        ) noexcept {
        return epoll_wait(EpollFd, lpCompletionPortEntries, ulCount, dwMilliseconds) > 0;
    }
    static MSH3_SQE* GetSqe(MSH3_CQE* Cqe) noexcept {
        return (MSH3_SQE*)Cqe->data.ptr;
    }
#elif __APPLE__ || __FreeBSD__
    // macOS or FreeBSD-specific implementation
    int KqueueFd;
    operator MSH3_EVENTQ* () noexcept { return &KqueueFd; }
    MsH3EventQueue() : KqueueFd(kqueue()) { }
    ~MsH3EventQueue() { close(KqueueFd); }
    bool IsValid() const noexcept { return KqueueFd >= 0; }
    bool Enqueue(
        MSH3_SQE* Sqe
        ) noexcept {
        struct kevent event = {.ident = Sqe->Handle, .filter = EVFILT_USER, .flags = EV_ADD | EV_ONESHOT, .fflags = NOTE_TRIGGER, .data = 0, .udata = Sqe};
        return kevent(KqueueFd, &event, 1, NULL, 0, NULL) == 0;
    }
    bool Dequeue(
        MSH3_CQE* lpCompletionPortEntries,
        uint32_t ulCount,
        uint32_t* ulNumEntriesRemoved,
        uint32_t dwMilliseconds
        ) noexcept {
        return kevent(KqueueFd, nullptr, 0, lpCompletionPortEntries, ulCount, nullptr) > 0;
    }
    static MSH3_SQE* GetSqe(MSH3_CQE* Cqe) noexcept {
        return CONTAINING_RECORD(Cqe, MSH3_SQE, Handle);
    }
#endif // _WIN32
    void CompleteEvents(uint32_t WaitTime) noexcept {
        uint32_t EventCount = 0;
        MSH3_CQE Events[8];
        if (Dequeue(Events, ARRAYSIZE(Events), &EventCount, WaitTime)) {
            for (uint32_t i = 0; i < EventCount; ++i) {
                MSH3_SQE* Sqe = GetSqe(&Events[i]);
                Sqe->Completion(&Events[i]);
            }
        }
    }
};

struct MsH3Api {
    MSH3_API* Handle { nullptr };
    MsH3Api() noexcept : Handle(MsH3ApiOpen()) { }
#ifdef MSH3_API_ENABLE_PREVIEW_FEATURES
    MsH3Api(
        uint32_t ExecutionConfigCount,
        MSH3_EXECUTION_CONFIG* ExecutionConfigs,
        MSH3_EXECUTION** Executions
        ) noexcept : Handle(MsH3ApiOpenWithExecution(ExecutionConfigCount, ExecutionConfigs, Executions)) {
    }
    uint32_t Poll(MSH3_EXECUTION* Execution) noexcept {
        return MsH3ApiPoll(Execution);
    }
#endif
    ~MsH3Api() noexcept { if (Handle) { MsH3ApiClose(Handle); } }
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_API* () const noexcept { return Handle; }
};

struct MsH3Configuration {
    MSH3_CONFIGURATION* Handle { nullptr };
    MsH3Configuration(MsH3Api& Api) {
        Handle = MsH3ConfigurationOpen(Api, nullptr, 0);
    }
    MsH3Configuration(MsH3Api& Api, const MSH3_SETTINGS* Settings) {
        Handle = MsH3ConfigurationOpen(Api, Settings, sizeof(*Settings));
    }
    ~MsH3Configuration() noexcept { if (Handle) { MsH3ConfigurationClose(Handle); } }
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_CONFIGURATION* () const noexcept { return Handle; }
#if MSH3_TEST_MODE
    MSH3_STATUS LoadConfiguration() noexcept {
        const MSH3_CREDENTIAL_CONFIG Config = { MSH3_CREDENTIAL_TYPE_SELF_SIGNED_CERTIFICATE };
        return MsH3ConfigurationLoadCredential(Handle, &Config);
    }
#endif
    MSH3_STATUS LoadConfiguration(const MSH3_CREDENTIAL_CONFIG& Config) noexcept {
        return MsH3ConfigurationLoadCredential(Handle, &Config);
    }
};

struct MsH3Addr {
    MSH3_ADDR Addr {0};
    MsH3Addr(uint16_t Port TEST_DEF(4433)) {
        SetPort(Port);
    }
    operator const MSH3_ADDR* () const noexcept { return &Addr; }
    void SetPort(uint16_t Port) noexcept { MSH3_SET_PORT(&Addr, Port); }
};

typedef MSH3_STATUS MsH3ListenerCallback(
    struct MsH3Listener* Listener,
    void* Context,
    MSH3_LISTENER_EVENT* Event
    );

struct MsH3Listener {
    MSH3_LISTENER* Handle { nullptr };
    MsH3CleanUpMode CleanUpMode;
    MsH3ListenerCallback* Callback{ nullptr };
    void* Context{ nullptr };
    MsH3Listener(
        MsH3Api& Api,
        const MsH3Addr& Address,
        MsH3CleanUpMode CleanUpMode,
        MsH3ListenerCallback* Callback,
        void* Context = nullptr
        ) noexcept : CleanUpMode(CleanUpMode), Callback(Callback), Context(Context) {
        Handle = MsH3ListenerOpen(Api, Address, (MSH3_LISTENER_CALLBACK_HANDLER)MsH3Callback, this);
    }
    ~MsH3Listener() noexcept { if (Handle) { MsH3ListenerClose(Handle); } }
    MsH3Listener(MsH3Listener& other) = delete;
    MsH3Listener operator=(MsH3Listener& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_LISTENER* () const noexcept { return Handle; }
private:
    static
    MSH3_STATUS
    MsH3Callback(
        MSH3_LISTENER* /* Listener */,
        MsH3Listener* pThis,
        MSH3_LISTENER_EVENT* Event
        ) noexcept {
        auto DeleteOnExit =
            Event->Type == MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE &&
            pThis->CleanUpMode == CleanUpAutoDelete;
        auto Status = pThis->Callback(pThis, pThis->Context, Event);
        if (DeleteOnExit) {
            delete pThis;
        }
        return Status;
    }
};

typedef MSH3_STATUS MsH3ConnectionCallback(
    struct MsH3Connection* Connection,
    void* Context,
    MSH3_CONNECTION_EVENT* Event
    );

struct MsH3Connection {
    MSH3_CONNECTION* Handle { nullptr };
    MsH3Waitable<bool> Connected;
    MsH3Waitable<bool> ShutdownComplete;
    MsH3Connection(
        MsH3Api& Api,
        MsH3CleanUpMode CleanUpMode = CleanUpManual,
        MsH3ConnectionCallback* Callback = NoOpCallback,
        void* Context = nullptr
        ) noexcept : CleanUp(CleanUpMode), Callback(Callback), Context(Context) {
        Handle = MsH3ConnectionOpen(Api, (MSH3_CONNECTION_CALLBACK_HANDLER)MsH3Callback, this);
    }
    MsH3Connection(
        MSH3_CONNECTION* ServerHandle,
        MsH3CleanUpMode CleanUpMode,
        MsH3ConnectionCallback* Callback,
        void* Context = nullptr
        ) noexcept : Handle(ServerHandle), CleanUp(CleanUpMode), Callback(Callback), Context(Context)  {
        MsH3ConnectionSetCallbackHandler(Handle, (MSH3_CONNECTION_CALLBACK_HANDLER)MsH3Callback, this);
    }
    ~MsH3Connection() noexcept { Close(); }
    MsH3Connection(MsH3Connection& other) = delete;
    MsH3Connection operator=(MsH3Connection& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_CONNECTION* () const noexcept { return Handle; }
    void Close() noexcept {
#ifdef _WIN32
        auto HandleToClose = (MSH3_CONNECTION*)InterlockedExchangePointer((PVOID*)&Handle, NULL);
#else
        auto HandleToClose = (MSH3_CONNECTION*)__sync_fetch_and_and(&Handle, 0);
#endif
        if (HandleToClose) {
            MsH3ConnectionClose(HandleToClose);
        }
    }
    MSH3_STATUS SetConfiguration(const MsH3Configuration& Configuration) noexcept {
        return MsH3ConnectionSetConfiguration(Handle, Configuration);
    }
    MSH3_STATUS Start(
        const MsH3Configuration& Configuration,
        const char* ServerName TEST_DEF("localhost"),
        const MsH3Addr& ServerAddress TEST_DEF(MsH3Addr())
        ) noexcept {
        return MsH3ConnectionStart(Handle, Configuration, ServerName, ServerAddress);
    }
    void Shutdown(uint64_t ErrorCode = 0) noexcept {
        MsH3ConnectionShutdown(Handle, ErrorCode);
    }
    static
    MSH3_STATUS
    NoOpCallback(
        MsH3Connection* /* Connection */,
        void* /* Context */,
        MSH3_CONNECTION_EVENT* Event
        ) noexcept {
        if (Event->Type == MSH3_CONNECTION_EVENT_NEW_REQUEST) {
            //
            // Not great beacuse it doesn't provide an application specific
            // error code. If you expect to get streams, you should not no-op
            // the callbacks.
            //
            MsH3RequestClose(Event->NEW_REQUEST.Request);
        }
        return MSH3_STATUS_SUCCESS;
    }
private:
    const MsH3CleanUpMode CleanUp;
    MsH3ConnectionCallback* Callback;
    void* Context;
    static
    MSH3_STATUS
    MsH3Callback(
        MSH3_CONNECTION* /* Connection */,
        MsH3Connection* pThis,
        MSH3_CONNECTION_EVENT* Event
        ) noexcept {
        if (Event->Type == MSH3_CONNECTION_EVENT_CONNECTED) {
            pThis->Connected.Set(true);
        } else if (Event->Type == MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE) {
            pThis->ShutdownComplete.Set(true);
        }
        auto DeleteOnExit =
            Event->Type == MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE &&
            pThis->CleanUp == CleanUpAutoDelete;
        auto Status = pThis->Callback(pThis, pThis->Context, Event);
        if (DeleteOnExit) {
            delete pThis;
        }
        return Status;
    }
};

struct MsH3AutoAcceptListener : public MsH3Listener {
    const MsH3Configuration* Configuration;
    MsH3ConnectionCallback* ConnectionHandler;
    void* ConnectionContext;
    MsH3Waitable<MsH3Connection*> NewConnection;

    MsH3AutoAcceptListener(
        MsH3Api& Api,
        const MsH3Addr& Address,
        MsH3ConnectionCallback* _ConnectionHandler,
        void* _ConnectionContext = nullptr
        ) noexcept :
        MsH3Listener(Api, Address, CleanUpManual, ListenerCallback, this),
        Configuration(nullptr),
        ConnectionHandler(_ConnectionHandler),
        ConnectionContext(_ConnectionContext)
    { }

    MsH3AutoAcceptListener(
        MsH3Api& Api,
        const MsH3Addr& Address,
        const MsH3Configuration& Config,
        MsH3ConnectionCallback* _ConnectionHandler,
        void* _ConnectionContext = nullptr
        ) noexcept :
        MsH3Listener(Api, Address, CleanUpManual, ListenerCallback, this),
        Configuration(&Config),
        ConnectionHandler(_ConnectionHandler),
        ConnectionContext(_ConnectionContext)
    { }

private:

    static
    MSH3_STATUS
    ListenerCallback(
        MsH3Listener* /* Listener */,
        void* Context,
        MSH3_LISTENER_EVENT* Event
        ) noexcept {
        auto pThis = (MsH3AutoAcceptListener*)Context;
#ifdef _WIN32
        MSH3_STATUS Status = E_NOT_VALID_STATE;
#else
        MSH3_STATUS Status = EINVAL;
#endif
        if (Event->Type == MSH3_LISTENER_EVENT_NEW_CONNECTION) {
            auto Connection = new(std::nothrow) MsH3Connection(Event->NEW_CONNECTION.Connection, CleanUpAutoDelete, pThis->ConnectionHandler, pThis->ConnectionContext);
            if (Connection) {
                if (pThis->Configuration &&
                    MSH3_FAILED(Status = Connection->SetConfiguration(*pThis->Configuration))) {
                    //
                    // The connection is being rejected. Let the library free the handle.
                    //
                    Connection->Handle = nullptr;
                    delete Connection;
                } else {
                    Status = MSH3_STATUS_SUCCESS;
                    pThis->NewConnection.Set(Connection);
                }
            }
        }
        return Status;
    }
};

typedef MSH3_STATUS MsH3RequestCallback(
    struct MsH3Request* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    );

struct MsH3Request {
    MSH3_REQUEST* Handle { nullptr };
    MsH3CleanUpMode CleanUpMode;
    MsH3RequestCallback* Callback;
    void* Context;
    MsH3Waitable<bool> ShutdownComplete;
    bool Aborted {false};
    uint64_t AbortError {0};
    MsH3Request(
        MsH3Connection& Connection,
        MSH3_REQUEST_FLAGS Flags = MSH3_REQUEST_FLAG_NONE,
        MsH3CleanUpMode CleanUpMode = CleanUpManual,
        MsH3RequestCallback* Callback = NoOpCallback,
        void* Context = nullptr
        ) noexcept : CleanUpMode(CleanUpMode), Callback(Callback), Context(Context) {
        if (Connection.IsValid()) {
            Handle = MsH3RequestOpen(Connection, (MSH3_REQUEST_CALLBACK_HANDLER)MsH3Callback, this, Flags);
        }
    }
    MsH3Request(
        MSH3_REQUEST* ServerHandle,
        MsH3CleanUpMode CleanUpMode,
        MsH3RequestCallback* Callback = NoOpCallback,
        void* Context = nullptr
        ) noexcept : Handle(ServerHandle), CleanUpMode(CleanUpMode), Callback(Callback), Context(Context) {
        MsH3RequestSetCallbackHandler(Handle, (MSH3_REQUEST_CALLBACK_HANDLER)MsH3Callback, this);
    }
    ~MsH3Request() noexcept { Close(); }
    MsH3Request(MsH3Request& other) = delete;
    MsH3Request operator=(MsH3Request& Other) = delete;
    bool IsValid() const noexcept { return Handle != nullptr; }
    operator MSH3_REQUEST* () const noexcept { return Handle; }
    void
    Close() noexcept {
#ifdef _WIN32
        auto HandleToClose = (MSH3_REQUEST*)InterlockedExchangePointer((PVOID*)&Handle, NULL);
#else
        auto HandleToClose = (MSH3_REQUEST*)__sync_fetch_and_and(&Handle, 0);
#endif
        if (HandleToClose) {
            MsH3RequestClose(HandleToClose);
        }
    }
    void CompleteReceive(uint32_t Length) noexcept {
        MsH3RequestCompleteReceive(Handle, Length);
    };
    void SetReceiveEnabled(bool Enabled) noexcept {
        MsH3RequestSetReceiveEnabled(Handle, Enabled);
    };
    bool Send(
        const MSH3_HEADER* Headers,
        size_t HeadersCount,
        const void* Data = nullptr,
        uint32_t DataLength = 0,
        MSH3_REQUEST_SEND_FLAGS Flags = MSH3_REQUEST_SEND_FLAG_NONE,
        void* SendContext = nullptr
        ) noexcept {
        return MsH3RequestSend(Handle, Flags, Headers, HeadersCount, Data, DataLength, SendContext);
    }
    void Shutdown(
        MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
        uint64_t _AbortError = 0
        ) noexcept {
        return MsH3RequestShutdown(Handle, Flags, _AbortError);
    }
    static
    MSH3_STATUS
    NoOpCallback(
        MsH3Request* /* Request */,
        void* /* Context */,
        MSH3_REQUEST_EVENT* /* Event */
        ) noexcept {
        return MSH3_STATUS_SUCCESS;
    }
private:
    static
    MSH3_STATUS
    MsH3Callback(
        MSH3_REQUEST* /* Request */,
        MsH3Request* pThis,
        MSH3_REQUEST_EVENT* Event
        ) noexcept {
        if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE) {
            pThis->ShutdownComplete.Set(true);
        }
        auto DeleteOnExit =
            Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE &&
            pThis->CleanUpMode == CleanUpAutoDelete;
        auto Status = pThis->Callback(pThis, pThis->Context, Event);
        if (DeleteOnExit) {
            delete pThis;
        }
        return Status;
    }
};
