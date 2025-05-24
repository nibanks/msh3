# Request API

The Request API provides functions for creating and managing HTTP/3 requests.

## MsH3RequestOpen

```c
MSH3_REQUEST*
MSH3_CALL
MsH3RequestOpen(
    MSH3_CONNECTION* Connection,
    const MSH3_REQUEST_CALLBACK_HANDLER Handler,
    void* Context,
    MSH3_REQUEST_FLAGS Flags
    );
```

Creates a new HTTP/3 request object.

### Parameters

`Connection` - The connection object.

`Handler` - A callback function that will be invoked when request events occur.

`Context` - A user-provided context pointer that will be passed to the callback function.

`Flags` - Flags to control request behavior.

### Returns

Returns a pointer to the new request object, or NULL if the operation fails.

### Remarks

This function creates a request object. To send headers and data on the request, use MsH3RequestSend.

### Example

```c
MSH3_STATUS
RequestCallback(
    MSH3_REQUEST* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        printf("Header received: %.*s: %.*s\n", 
               (int)Event->HEADER_RECEIVED.Header->NameLength,
               Event->HEADER_RECEIVED.Header->Name,
               (int)Event->HEADER_RECEIVED.Header->ValueLength,
               Event->HEADER_RECEIVED.Header->Value);
        break;
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        printf("Data received: %u bytes\n", Event->DATA_RECEIVED.Length);
        // Process data...
        break;
    // Handle other events...
    }
    return MSH3_STATUS_SUCCESS;
}

MSH3_REQUEST* request = 
    MsH3RequestOpen(connection, RequestCallback, myContext, 
                   MSH3_REQUEST_FLAG_NONE);
if (request == NULL) {
    printf("Failed to create request\n");
    return 1;
}
```

## MsH3RequestSetCallbackHandler

```c
void
MSH3_CALL
MsH3RequestSetCallbackHandler(
    MSH3_REQUEST* Request,
    const MSH3_REQUEST_CALLBACK_HANDLER Handler,
    void* Context
    );
```

Sets or updates the callback handler for a request.

### Parameters

`Request` - The request object.

`Handler` - The new callback handler.

`Context` - The new context pointer to be passed to the callback.

### Remarks

This function can be used to change the callback handler after a request has been created.

### Example

```c
// Update the callback handler
MsH3RequestSetCallbackHandler(request, NewCallback, newContext);
```

## MsH3RequestSend

```c
bool
MSH3_CALL
MsH3RequestSend(
    MSH3_REQUEST* Request,
    MSH3_REQUEST_SEND_FLAGS Flags,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    const void* Data,
    uint32_t DataLength,
    void* AppContext
    );
```

Sends headers and optional data on a request.

### Parameters

`Request` - The request object.

`Flags` - Flags to control the send operation.

`Headers` - An array of HTTP headers to send.

`HeadersCount` - The number of headers in the array.

`Data` - Optional data to send after the headers.

`DataLength` - The length of the data.

`AppContext` - An application context pointer that will be returned in the SEND_COMPLETE event.

### Returns

Returns true if the send operation was successfully queued, false otherwise.

### Remarks

This function sends headers and optional data on a request. The headers must include the required HTTP/3 pseudo-headers (`:method`, `:path`, `:scheme`, `:authority`).

If the MSH3_REQUEST_SEND_FLAG_FIN flag is specified, the request is marked as complete and no more data can be sent.

### Example

```c
const MSH3_HEADER headers[] = {
    { ":method", 7, "GET", 3 },
    { ":path", 5, "/index.html", 11 },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, "example.com", 11 },
    { "user-agent", 10, "msh3-client", 11 }
};
const size_t headersCount = sizeof(headers) / sizeof(MSH3_HEADER);

bool success = MsH3RequestSend(
    request,
    MSH3_REQUEST_SEND_FLAG_FIN,  // No more data to send
    headers,
    headersCount,
    NULL,  // No body data
    0,
    NULL);

if (!success) {
    printf("Failed to send request\n");
    return 1;
}
```

## MsH3RequestSetReceiveEnabled

```c
void
MSH3_CALL
MsH3RequestSetReceiveEnabled(
    MSH3_REQUEST* Request,
    bool Enabled
    );
```

Enables or disables receive operations on a request.

### Parameters

`Request` - The request object.

`Enabled` - Whether to enable or disable receiving.

### Remarks

This function can be used to implement flow control. When receive is disabled, the peer will stop sending data once its flow control window is exhausted.

### Example

```c
// Temporarily disable receiving to handle backpressure
MsH3RequestSetReceiveEnabled(request, false);

// Later, when ready to receive more data:
MsH3RequestSetReceiveEnabled(request, true);
```

## MsH3RequestCompleteReceive

```c
void
MSH3_CALL
MsH3RequestCompleteReceive(
    MSH3_REQUEST* Request,
    uint32_t Length
    );
```

Completes a receive operation.

### Parameters

`Request` - The request object.

`Length` - The number of bytes consumed.

### Remarks

This function should be called after processing data received in a MSH3_REQUEST_EVENT_DATA_RECEIVED event. It updates the flow control window.

### Example

```c
MSH3_STATUS
RequestCallback(
    MSH3_REQUEST* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        // Process data...
        
        // Indicate that we've consumed the data
        MsH3RequestCompleteReceive(Request, Event->DATA_RECEIVED.Length);
        break;
    // Handle other events...
    }
    return MSH3_STATUS_SUCCESS;
}
```

## MsH3RequestShutdown

```c
void
MSH3_CALL
MsH3RequestShutdown(
    MSH3_REQUEST* Request,
    MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
    uint64_t AbortError
    );
```

Initiates shutdown of a request.

### Parameters

`Request` - The request object.

`Flags` - Flags indicating how to shutdown the request.

`AbortError` - An error code to send to the peer, used only with abort flags.

### Remarks

This function initiates the shutdown process for a request. The request is not fully closed until the MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE event is received by the callback.

### Example

```c
// Gracefully shutdown the request
MsH3RequestShutdown(
    request,
    MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL,
    0);  // Ignored for graceful shutdown

// Abortively shutdown the request
MsH3RequestShutdown(
    request,
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT,
    0x10);  // Application-defined error code
```

## MsH3RequestClose

```c
void
MSH3_CALL
MsH3RequestClose(
    MSH3_REQUEST* Request
    );
```

Closes a request and releases associated resources.

### Parameters

`Request` - The request object to close.

### Remarks

This function should be called when the request is no longer needed. After calling this function, the request handle is no longer valid.

### Example

```c
// Close the request
MsH3RequestClose(request);
```
