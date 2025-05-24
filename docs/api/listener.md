# Listener API

The Listener API provides functions for creating and managing HTTP/3 listeners for server applications.

## MsH3ListenerOpen

```c
MSH3_LISTENER*
MSH3_CALL
MsH3ListenerOpen(
    MSH3_API* Api,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_CALLBACK_HANDLER Handler,
    void* Context
    );
```

Creates a new HTTP/3 listener object.

### Parameters

`Api` - The API object.

`Address` - The local address and port to bind to.

`Handler` - A callback function that will be invoked when listener events occur.

`Context` - A user-provided context pointer that will be passed to the callback function.

### Returns

Returns a pointer to the new listener object, or NULL if the operation fails.

### Remarks

This function creates a listener for incoming HTTP/3 connections. The listener will immediately begin accepting connections after creation.

When a new connection is accepted, the listener callback will be invoked with a MSH3_LISTENER_EVENT_NEW_CONNECTION event.

### Example

```c
MSH3_STATUS
ListenerCallback(
    MSH3_LISTENER* Listener,
    void* Context,
    MSH3_LISTENER_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_LISTENER_EVENT_NEW_CONNECTION:
        printf("New connection from %s\n", Event->NEW_CONNECTION.ServerName);
        
        // Set up the connection callback
        MsH3ConnectionSetCallbackHandler(
            Event->NEW_CONNECTION.Connection,
            ConnectionCallback,
            myContext);
        
        // The connection is now handed over to the application
        
        break;
    case MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE:
        printf("Listener shutdown complete\n");
        break;
    }
    return MSH3_STATUS_SUCCESS;
}

// Set up the local address
MSH3_ADDR localAddr = {0};
localAddr.Ipv4.sin_family = AF_INET;
localAddr.Ipv4.sin_addr.s_addr = INADDR_ANY;
MSH3_SET_PORT(&localAddr, 443);

MSH3_LISTENER* listener = 
    MsH3ListenerOpen(api, &localAddr, ListenerCallback, myContext);
if (listener == NULL) {
    printf("Failed to create listener\n");
    return 1;
}
```

## MsH3ListenerClose

```c
void
MSH3_CALL
MsH3ListenerClose(
    MSH3_LISTENER* Listener
    );
```

Closes a listener and releases associated resources.

### Parameters

`Listener` - The listener object to close.

### Remarks

This function should be called when the listener is no longer needed. After calling this function, the listener handle is no longer valid.

Closing a listener does not affect any connections that were previously accepted by the listener.

### Example

```c
// Close the listener
MsH3ListenerClose(listener);
```
