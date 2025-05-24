# Using MSH3 as a Server

This guide explains how to use MSH3 as an HTTP/3 server.

## Basic Server Usage

Here's a step-by-step guide to using MSH3 as an HTTP/3 server:

1. Initialize the MSH3 API
2. Create a configuration with appropriate settings and credentials
3. Create a listener to accept incoming connections
4. Process incoming connections in the listener callback
5. Handle requests on each connection
6. Send responses to requests
7. Clean up resources

## Example

```c
#include "msh3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Callback for request events
MSH3_STATUS
RequestCallback(
    MSH3_REQUEST* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        printf("Header: %.*s: %.*s\n", 
               (int)Event->HEADER_RECEIVED.Header->NameLength,
               Event->HEADER_RECEIVED.Header->Name,
               (int)Event->HEADER_RECEIVED.Header->ValueLength,
               Event->HEADER_RECEIVED.Header->Value);
        break;
        
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        printf("Received %u bytes of data\n", Event->DATA_RECEIVED.Length);
        printf("%.*s", (int)Event->DATA_RECEIVED.Length, 
                      (const char*)Event->DATA_RECEIVED.Data);
        
        // Indicate that we've processed the data
        MsH3RequestCompleteReceive(Request, Event->DATA_RECEIVED.Length);
        break;
        
    case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN:
        printf("Client completed sending data\n");
        
        // Client has finished sending the request, send a response
        const MSH3_HEADER ResponseHeaders[] = {
            { ":status", 7, "200", 3 },
            { "content-type", 12, "text/plain", 10 }
        };
        const size_t ResponseHeadersCount = sizeof(ResponseHeaders) / sizeof(MSH3_HEADER);
        
        const char* responseBody = "Hello from MSH3 server!";
        
        MsH3RequestSend(
            Request,
            MSH3_REQUEST_SEND_FLAG_FIN,
            ResponseHeaders,
            ResponseHeadersCount,
            responseBody,
            (uint32_t)strlen(responseBody),
            NULL);
        break;
        
    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        printf("Request shutdown complete\n");
        break;
    }
    
    return MSH3_STATUS_SUCCESS;
}

// Callback for connection events
MSH3_STATUS
ConnectionCallback(
    MSH3_CONNECTION* Connection,
    void* Context,
    MSH3_CONNECTION_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_CONNECTION_EVENT_NEW_REQUEST:
        printf("New request received\n");
        
        // Set up the request callback
        MSH3_REQUEST* request = Event->NEW_REQUEST.Request;
        MsH3RequestSetCallbackHandler(
            request,
            RequestCallback,
            NULL);
            
        // Enable receiving data on the request
        MsH3RequestSetReceiveEnabled(request, true);
        break;
        
    case MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        printf("Connection shutdown by transport, status=0x%x, error=0x%llx\n", 
               Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status,
               (unsigned long long)Event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode);
        break;
        
    case MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("Connection shutdown by peer, error=0x%llx\n", 
               (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;
        
    case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        printf("Connection shutdown complete\n");
        break;
    }
    
    return MSH3_STATUS_SUCCESS;
}

// Callback for listener events
MSH3_STATUS
ListenerCallback(
    MSH3_LISTENER* Listener,
    void* Context,
    MSH3_LISTENER_EVENT* Event
    )
{
    MSH3_CONFIGURATION* config = (MSH3_CONFIGURATION*)Context;
    
    switch (Event->Type) {
    case MSH3_LISTENER_EVENT_NEW_CONNECTION:
        printf("New connection from %.*s\n", 
               (int)Event->NEW_CONNECTION.ServerNameLength,
               Event->NEW_CONNECTION.ServerName);
        
        // Set up the connection callback
        MsH3ConnectionSetCallbackHandler(
            Event->NEW_CONNECTION.Connection,
            ConnectionCallback,
            NULL);
        
        // Apply configuration to the connection
        MSH3_STATUS status = 
            MsH3ConnectionSetConfiguration(
                Event->NEW_CONNECTION.Connection,
                config);
                
        if (MSH3_FAILED(status)) {
            printf("Failed to configure connection, status=0x%x\n", status);
            MsH3ConnectionClose(Event->NEW_CONNECTION.Connection);
        }
        break;
        
    case MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE:
        printf("Listener shutdown complete\n");
        break;
    }
    
    return MSH3_STATUS_SUCCESS;
}

int main(int argc, char** argv) {
    uint16_t port = (argc > 1) ? (uint16_t)atoi(argv[1]) : 443;
    
    // Initialize MSH3 API
    MSH3_API* api = MsH3ApiOpen();
    if (!api) {
        printf("Failed to initialize MSH3 API\n");
        return 1;
    }
    
    // Create configuration
    MSH3_SETTINGS settings = {0};
    settings.IsSetFlags = 0;
    settings.IsSet.IdleTimeoutMs = 1;
    settings.IdleTimeoutMs = 30000;  // 30 seconds
    
    MSH3_CONFIGURATION* config = 
        MsH3ConfigurationOpen(api, &settings, sizeof(settings));
    if (!config) {
        printf("Failed to create configuration\n");
        MsH3ApiClose(api);
        return 1;
    }
    
    // Load server certificate and private key
    MSH3_CERTIFICATE_FILE certFile = {
        .PrivateKeyFile = "server.key",
        .CertificateFile = "server.crt"
    };
    
    MSH3_CREDENTIAL_CONFIG credConfig = {
        .Type = MSH3_CREDENTIAL_TYPE_CERTIFICATE_FILE,
        .Flags = MSH3_CREDENTIAL_FLAG_NONE,  // Server mode (no client flag)
        .CertificateFile = &certFile
    };
    
    MSH3_STATUS status = MsH3ConfigurationLoadCredential(config, &credConfig);
    if (MSH3_FAILED(status)) {
        printf("Failed to load credentials, status=0x%x\n", status);
        MsH3ConfigurationClose(config);
        MsH3ApiClose(api);
        return 1;
    }
    
    // Set up the local address
    MSH3_ADDR localAddr = {0};
    localAddr.Ipv4.sin_family = AF_INET;
    localAddr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    MSH3_SET_PORT(&localAddr, port);
    
    // Create listener
    MSH3_LISTENER* listener = 
        MsH3ListenerOpen(api, &localAddr, ListenerCallback, config);
    if (!listener) {
        printf("Failed to create listener\n");
        MsH3ConfigurationClose(config);
        MsH3ApiClose(api);
        return 1;
    }
    
    printf("HTTP/3 server listening on port %u\n", port);
    printf("Press Enter to terminate...\n");
    getchar();
    
    // Clean up
    MsH3ListenerClose(listener);
    MsH3ConfigurationClose(config);
    MsH3ApiClose(api);
    
    return 0;
}
```

## Advanced Features

### Requiring Client Authentication

To require clients to present a certificate:

```c
// Set the server to require client authentication
credConfig.Flags = MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;
```

### Customizing Server Settings

```c
MSH3_SETTINGS settings = {0};
settings.IsSet.IdleTimeoutMs = 1;
settings.IdleTimeoutMs = 60000;  // 60 seconds idle timeout

settings.IsSet.PeerRequestCount = 1;
settings.PeerRequestCount = 100;  // Allow up to 100 concurrent requests per connection

settings.IsSet.DatagramEnabled = 1;
settings.DatagramEnabled = 1;  // Enable QUIC datagrams
```

### Handling Different Types of Requests

```c
// In the RequestCallback function
if (strncmp(Event->HEADER_RECEIVED.Header->Name, ":path", 5) == 0) {
    const char* path = Event->HEADER_RECEIVED.Header->Value;
    size_t pathLength = Event->HEADER_RECEIVED.Header->ValueLength;
    
    // Store the path for later use
    char* requestPath = malloc(pathLength + 1);
    if (requestPath) {
        memcpy(requestPath, path, pathLength);
        requestPath[pathLength] = '\0';
        
        // Store in the request context
        MsH3RequestSetCallbackHandler(Request, RequestCallback, requestPath);
    }
}

// When sending the response
void* context = Context;  // This is the path we stored
const char* requestPath = (const char*)context;

// Different response based on the path
if (strcmp(requestPath, "/api/data") == 0) {
    // API response
    const char* jsonResponse = "{\"status\":\"success\",\"data\":{}}";
    
    const MSH3_HEADER ResponseHeaders[] = {
        { ":status", 7, "200", 3 },
        { "content-type", 12, "application/json", 16 }
    };
    
    MsH3RequestSend(
        Request,
        MSH3_REQUEST_SEND_FLAG_FIN,
        ResponseHeaders,
        sizeof(ResponseHeaders) / sizeof(MSH3_HEADER),
        jsonResponse,
        (uint32_t)strlen(jsonResponse),
        NULL);
} else {
    // Default response
    const char* textResponse = "404 Not Found";
    
    const MSH3_HEADER ResponseHeaders[] = {
        { ":status", 7, "404", 3 },
        { "content-type", 12, "text/plain", 10 }
    };
    
    MsH3RequestSend(
        Request,
        MSH3_REQUEST_SEND_FLAG_FIN,
        ResponseHeaders,
        sizeof(ResponseHeaders) / sizeof(MSH3_HEADER),
        textResponse,
        (uint32_t)strlen(textResponse),
        NULL);
}

// Clean up the context
free(requestPath);
```

### Responding with a File

```c
// Function to send a file as response
void SendFileResponse(MSH3_REQUEST* Request, const char* filePath, const char* contentType) {
    FILE* file = fopen(filePath, "rb");
    if (!file) {
        // Send 404 response
        const MSH3_HEADER ErrorHeaders[] = {
            { ":status", 7, "404", 3 },
            { "content-type", 12, "text/plain", 10 }
        };
        
        const char* errorMsg = "404 Not Found";
        
        MsH3RequestSend(
            Request,
            MSH3_REQUEST_SEND_FLAG_FIN,
            ErrorHeaders,
            sizeof(ErrorHeaders) / sizeof(MSH3_HEADER),
            errorMsg,
            (uint32_t)strlen(errorMsg),
            NULL);
        return;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file content
    char* buffer = (char*)malloc(fileSize);
    if (!buffer) {
        fclose(file);
        return;
    }
    
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    fclose(file);
    
    // Send response
    const MSH3_HEADER Headers[] = {
        { ":status", 7, "200", 3 },
        { "content-type", 12, contentType, strlen(contentType) }
    };
    
    MsH3RequestSend(
        Request,
        MSH3_REQUEST_SEND_FLAG_FIN,
        Headers,
        sizeof(Headers) / sizeof(MSH3_HEADER),
        buffer,
        (uint32_t)bytesRead,
        buffer);  // Use buffer as context to free it later
        
    // Buffer will be freed in the SEND_COMPLETE event
}

// In the send complete event handler
case MSH3_REQUEST_EVENT_SEND_COMPLETE:
    if (Event->SEND_COMPLETE.ClientContext) {
        free(Event->SEND_COMPLETE.ClientContext);
    }
    break;
```
