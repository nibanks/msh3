# Connection API

The Connection API provides functions for creating and managing HTTP/3 connections.

## MsH3ConnectionOpen

```c
MSH3_CONNECTION*
MSH3_CALL
MsH3ConnectionOpen(
    MSH3_API* Api,
    const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context
    );
```

Creates a new HTTP/3 connection object.

### Parameters

`Api` - The API object.

`Handler` - A callback function that will be invoked when connection events occur.

`Context` - A user-provided context pointer that will be passed to the callback function.

### Returns

Returns a pointer to the new connection object, or NULL if the operation fails.

### Remarks

This function creates a connection object but does not start the connection. To start the connection, call MsH3ConnectionStart.

### Example

```c
MSH3_STATUS
ConnectionCallback(
    MSH3_CONNECTION* Connection,
    void* Context,
    MSH3_CONNECTION_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_CONNECTION_EVENT_CONNECTED:
        printf("Connection established!\n");
        break;
    case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        printf("Connection shutdown complete\n");
        break;
    // Handle other events...
    }
    return MSH3_STATUS_SUCCESS;
}

MSH3_CONNECTION* connection = 
    MsH3ConnectionOpen(api, ConnectionCallback, myContext);
if (connection == NULL) {
    printf("Failed to create connection\n");
    return 1;
}
```

## MsH3ConnectionSetCallbackHandler

```c
void
MSH3_CALL
MsH3ConnectionSetCallbackHandler(
    MSH3_CONNECTION* Connection,
    const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context
    );
```

Sets or updates the callback handler for a connection.

### Parameters

`Connection` - The connection object.

`Handler` - The new callback handler.

`Context` - The new context pointer to be passed to the callback.

### Remarks

This function can be used to change the callback handler after a connection has been created.

### Example

```c
// Update the callback handler
MsH3ConnectionSetCallbackHandler(connection, NewCallback, newContext);
```

## MsH3ConnectionSetConfiguration

```c
MSH3_STATUS
MSH3_CALL
MsH3ConnectionSetConfiguration(
    MSH3_CONNECTION* Connection,
    MSH3_CONFIGURATION* Configuration
    );
```

Sets the configuration for a connection.

### Parameters

`Connection` - The connection object.

`Configuration` - The configuration object to apply to the connection.

### Returns

Returns MSH3_STATUS_SUCCESS if successful, or an error code otherwise.

### Remarks

This function must be called before starting the connection.

### Example

```c
MSH3_STATUS status = 
    MsH3ConnectionSetConfiguration(connection, config);
if (MSH3_FAILED(status)) {
    printf("Failed to set configuration, status=0x%x\n", status);
    return 1;
}
```

## MsH3ConnectionStart

```c
MSH3_STATUS
MSH3_CALL
MsH3ConnectionStart(
    MSH3_CONNECTION* Connection,
    MSH3_CONFIGURATION* Configuration,
    const char* ServerName,
    const MSH3_ADDR* ServerAddress
    );
```

Starts a connection to a server.

### Parameters

`Connection` - The connection object.

`Configuration` - The configuration object to use for the connection.

`ServerName` - The server name (hostname) to connect to.

`ServerAddress` - The server address to connect to.

### Returns

Returns MSH3_STATUS_SUCCESS if the connection start was successfully initiated, or an error code otherwise.

### Remarks

This function starts the connection process. The actual connection establishment happens asynchronously, and the connection callback will be invoked with a MSH3_CONNECTION_EVENT_CONNECTED event when the connection is established.

### Example

```c
// Set up the server address
MSH3_ADDR serverAddr = {0};
serverAddr.Ipv4.sin_family = AF_INET;
inet_pton(AF_INET, "192.0.2.1", &serverAddr.Ipv4.sin_addr);
MSH3_SET_PORT(&serverAddr, 443);

MSH3_STATUS status = 
    MsH3ConnectionStart(connection, config, "example.com", &serverAddr);
if (MSH3_FAILED(status)) {
    printf("Failed to start connection, status=0x%x\n", status);
    return 1;
}
```

## MsH3ConnectionShutdown

```c
void
MSH3_CALL
MsH3ConnectionShutdown(
    MSH3_CONNECTION* Connection,
    uint64_t ErrorCode
    );
```

Initiates shutdown of a connection.

### Parameters

`Connection` - The connection object.

`ErrorCode` - An application-defined error code to send to the peer.

### Remarks

This function initiates the shutdown process for a connection. The connection is not fully closed until the MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE event is received by the callback.

### Example

```c
// Gracefully shutdown the connection
MsH3ConnectionShutdown(connection, 0);
```

## MsH3ConnectionClose

```c
void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Connection
    );
```

Closes a connection and releases associated resources.

### Parameters

`Connection` - The connection object to close.

### Remarks

This function should be called when the connection is no longer needed. After calling this function, the connection handle is no longer valid.

### Example

```c
// Close the connection
MsH3ConnectionClose(connection);
```
