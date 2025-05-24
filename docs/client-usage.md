# Using MSH3 as a Client

This guide explains how to use MSH3 as an HTTP/3 client.

## Basic Client Usage

Here's a step-by-step guide to using MSH3 as an HTTP/3 client:

1. Initialize the MSH3 API
2. Create a configuration with appropriate settings
3. Create a connection to the server
4. Create a request and send headers and data
5. Process response headers and data in the callbacks
6. Clean up resources

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
        printf("Server completed sending data\n");
        break;
        
    case MSH3_REQUEST_EVENT_SEND_COMPLETE:
        printf("Request sent completely\n");
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
    case MSH3_CONNECTION_EVENT_CONNECTED:
        printf("Connection established\n");
        
        // Create a request when connected
        const char* host = (const char*)Context;
        MSH3_REQUEST* Request = MsH3RequestOpen(
            Connection,
            RequestCallback,
            NULL,
            MSH3_REQUEST_FLAG_NONE);
            
        if (Request) {
            // Setup HTTP headers
            const MSH3_HEADER Headers[] = {
                { ":method", 7, "GET", 3 },
                { ":path", 5, "/", 1 },
                { ":scheme", 7, "https", 5 },
                { ":authority", 10, host, strlen(host) },
                { "user-agent", 10, "msh3-client", 11 }
            };
            const size_t HeadersCount = sizeof(Headers) / sizeof(MSH3_HEADER);
            
            // Send the request
            MsH3RequestSend(
                Request,
                MSH3_REQUEST_SEND_FLAG_FIN,  // No more data to send
                Headers,
                HeadersCount,
                NULL,  // No body data
                0,
                NULL);
        } else {
            printf("Failed to create request\n");
        }
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

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <hostname> [port]\n", argv[0]);
        return 1;
    }
    
    const char* hostname = argv[1];
    uint16_t port = (argc > 2) ? (uint16_t)atoi(argv[2]) : 443;
    
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
    
    // Create connection
    MSH3_CONNECTION* connection = 
        MsH3ConnectionOpen(api, ConnectionCallback, (void*)hostname);
    if (!connection) {
        printf("Failed to create connection\n");
        MsH3ConfigurationClose(config);
        MsH3ApiClose(api);
        return 1;
    }
    
    // Configure the connection
    MSH3_STATUS status = MsH3ConnectionSetConfiguration(connection, config);
    if (MSH3_FAILED(status)) {
        printf("Failed to set connection configuration, status=0x%x\n", status);
        MsH3ConnectionClose(connection);
        MsH3ConfigurationClose(config);
        MsH3ApiClose(api);
        return 1;
    }
    
    // Set up the server address
    MSH3_ADDR serverAddr = {0};
    serverAddr.Ipv4.sin_family = AF_INET;
    // Resolve hostname and set IP address (simplified here)
    inet_pton(AF_INET, "198.51.100.1", &serverAddr.Ipv4.sin_addr); // Example IP
    MSH3_SET_PORT(&serverAddr, port);
    
    // Start the connection
    status = MsH3ConnectionStart(connection, config, hostname, &serverAddr);
    if (MSH3_FAILED(status)) {
        printf("Failed to start connection, status=0x%x\n", status);
        MsH3ConnectionClose(connection);
        MsH3ConfigurationClose(config);
        MsH3ApiClose(api);
        return 1;
    }
    
    // In a real application, you'd wait for events using an event loop
    // For this example, we'll just wait for user input before cleaning up
    printf("Press Enter to terminate...\n");
    getchar();
    
    // Clean up
    MsH3ConnectionClose(connection);
    MsH3ConfigurationClose(config);
    MsH3ApiClose(api);
    
    return 0;
}
```

## Advanced Features

### Handling Credentials

```c
// Set up client authentication with a certificate
MSH3_CERTIFICATE_FILE certFile = {
    .PrivateKeyFile = "client.key",
    .CertificateFile = "client.crt"
};

MSH3_CREDENTIAL_CONFIG credConfig = {
    .Type = MSH3_CREDENTIAL_TYPE_CERTIFICATE_FILE,
    .Flags = MSH3_CREDENTIAL_FLAG_CLIENT,
    .CertificateFile = &certFile
};

status = MsH3ConfigurationLoadCredential(config, &credConfig);
if (MSH3_FAILED(status)) {
    printf("Failed to load credentials, status=0x%x\n", status);
    return 1;
}
```

### Sending POST Request with Data

```c
const MSH3_HEADER Headers[] = {
    { ":method", 7, "POST", 4 },
    { ":path", 5, "/api/data", 9 },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, host, strlen(host) },
    { "content-type", 12, "application/json", 16 }
};
const size_t HeadersCount = sizeof(Headers) / sizeof(MSH3_HEADER);

const char* body = "{\"key\":\"value\"}";
uint32_t bodyLength = (uint32_t)strlen(body);

// Send request with body
MsH3RequestSend(
    Request,
    MSH3_REQUEST_SEND_FLAG_FIN,  // No more data to send after this
    Headers,
    HeadersCount,
    body,  // Request body
    bodyLength,
    NULL);
```

### Sending Request in Multiple Parts

```c
// Send headers first
MsH3RequestSend(
    Request,
    MSH3_REQUEST_SEND_FLAG_NONE,  // Not the end of the request
    Headers,
    HeadersCount,
    NULL,  // No body data yet
    0,
    NULL);
    
// Then send the first part of the body
const char* bodyPart1 = "First part of the data";
MsH3RequestSend(
    Request,
    MSH3_REQUEST_SEND_FLAG_DELAY_SEND,  // More data coming
    NULL,  // No headers this time
    0,
    bodyPart1,
    (uint32_t)strlen(bodyPart1),
    NULL);
    
// Send the last part of the body and finish the request
const char* bodyPart2 = "Last part of the data";
MsH3RequestSend(
    Request,
    MSH3_REQUEST_SEND_FLAG_FIN,  // End of request
    NULL,
    0,
    bodyPart2,
    (uint32_t)strlen(bodyPart2),
    NULL);
```
