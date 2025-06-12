# Dynamic QPACK Header Compression

This document explains how to configure and use dynamic QPACK header compression in MSH3.

## Overview

QPACK is the header compression mechanism used in HTTP/3. MSH3 supports both static and dynamic QPACK compression:

- **Static QPACK (default)**: Uses only predefined static headers table. Simpler, lower latency per request.
- **Dynamic QPACK**: Uses both static and dynamic tables. Better compression efficiency for repeated headers.

## When to Use Dynamic QPACK

Dynamic QPACK is beneficial when:

- Making multiple requests with similar headers (e.g., REST API calls)
- Bandwidth efficiency is more important than minimal per-request latency
- Your application has predictable header patterns
- Network conditions favor compression over speed

## Enabling Dynamic QPACK

### Prerequisites

Dynamic QPACK is available only when `MSH3_API_ENABLE_PREVIEW_FEATURES` is defined during compilation.

### Configuration

```c
#ifdef MSH3_API_ENABLE_PREVIEW_FEATURES
#include "msh3.h"

// Create settings with dynamic QPACK enabled
MSH3_SETTINGS settings = {0};
settings.IsSet.DynamicQPackEnabled = 1;
settings.DynamicQPackEnabled = 1;

// Optional: Configure other settings
settings.IsSet.IdleTimeoutMs = 1;
settings.IdleTimeoutMs = 30000;  // 30 seconds

// Create configuration
MSH3_CONFIGURATION* config = MsH3ConfigurationOpen(api, &settings, sizeof(settings));
if (!config) {
    // Handle error
    return;
}

// Use the configuration with your connection
MSH3_CONNECTION* connection = MsH3ConnectionOpen(api, ConnectionCallback, context);
if (connection) {
    MsH3ConnectionSetConfiguration(connection, config);
    // ... rest of connection setup
}
#endif
```

## Default Values

When dynamic QPACK is enabled, MSH3 uses these default settings:

- **Dynamic Table Capacity**: 4096 bytes
- **Maximum Blocked Streams**: 100 streams

These values provide a good balance between compression efficiency and memory usage for most applications.

## Example: REST API Client

Here's a complete example of a client that benefits from dynamic QPACK:

```c
#ifdef MSH3_API_ENABLE_PREVIEW_FEATURES
#include "msh3.h"
#include <stdio.h>

typedef struct {
    int requestCount;
    bool allRequestsComplete;
} ApiClientContext;

MSH3_STATUS RequestCallback(MSH3_REQUEST* Request, void* Context, MSH3_REQUEST_EVENT* Event) {
    ApiClientContext* apiContext = (ApiClientContext*)Context;

    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        printf("Response header: %.*s: %.*s\n",
               (int)Event->HEADER_RECEIVED.Header->NameLength,
               Event->HEADER_RECEIVED.Header->Name,
               (int)Event->HEADER_RECEIVED.Header->ValueLength,
               Event->HEADER_RECEIVED.Header->Value);
        break;

    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        printf("Response data: %.*s\n",
               (int)Event->DATA_RECEIVED.Length,
               (const char*)Event->DATA_RECEIVED.Data);
        MsH3RequestCompleteReceive(Request, Event->DATA_RECEIVED.Length);
        break;

    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        apiContext->requestCount--;
        if (apiContext->requestCount == 0) {
            apiContext->allRequestsComplete = true;
        }
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

MSH3_STATUS ConnectionCallback(MSH3_CONNECTION* Connection, void* Context, MSH3_CONNECTION_EVENT* Event) {
    ApiClientContext* apiContext = (ApiClientContext*)Context;

    if (Event->Type == MSH3_CONNECTION_EVENT_CONNECTED) {
        printf("Connected! Making API requests...\n");

        // Make multiple API requests with similar headers
        // These will benefit from dynamic QPACK compression
        const char* apiEndpoints[] = {
            "/api/v1/users",
            "/api/v1/posts",
            "/api/v1/comments",
            "/api/v1/likes"
        };

        for (int i = 0; i < 4; i++) {
            MSH3_REQUEST* request = MsH3RequestOpen(Connection, RequestCallback, apiContext, MSH3_REQUEST_FLAG_NONE);
            if (request) {
                // Common headers that will be compressed efficiently with dynamic QPACK
                const MSH3_HEADER headers[] = {
                    { ":method", 7, "GET", 3 },
                    { ":path", 5, apiEndpoints[i], strlen(apiEndpoints[i]) },
                    { ":scheme", 7, "https", 5 },
                    { ":authority", 10, "api.example.com", 15 },
                    { "user-agent", 10, "MyApp/1.0 (HTTP3-Client)", 24 },
                    { "accept", 6, "application/json", 16 },
                    { "accept-encoding", 15, "gzip, deflate, br", 17 },
                    { "authorization", 13, "Bearer token123456789", 21 },
                    { "x-api-version", 13, "1.0", 3 },
                    { "x-client-id", 11, "client-12345", 12 }
                };

                MsH3RequestSend(request, MSH3_REQUEST_SEND_FLAG_FIN,
                               headers, sizeof(headers)/sizeof(MSH3_HEADER),
                               NULL, 0, NULL);
                apiContext->requestCount++;
            }
        }
    }

    return MSH3_STATUS_SUCCESS;
}

int main() {
    // Initialize MSH3
    MSH3_API* api = MsH3ApiOpen();
    if (!api) return 1;

    // Configure with dynamic QPACK
    MSH3_SETTINGS settings = {0};
    settings.IsSet.DynamicQPackEnabled = 1;
    settings.DynamicQPackEnabled = 1;
    settings.IsSet.IdleTimeoutMs = 1;
    settings.IdleTimeoutMs = 30000;

    MSH3_CONFIGURATION* config = MsH3ConfigurationOpen(api, &settings, sizeof(settings));
    if (!config) {
        MsH3ApiClose(api);
        return 1;
    }

    // Create connection
    ApiClientContext context = {0};
    MSH3_CONNECTION* connection = MsH3ConnectionOpen(api, ConnectionCallback, &context);
    if (!connection) {
        MsH3ConfigurationClose(config);
        MsH3ApiClose(api);
        return 1;
    }

    // Start connection
    MsH3ConnectionSetConfiguration(connection, config);

    MSH3_ADDR serverAddr = {0};
    // Set up server address...
    MsH3ConnectionStart(connection, config, "api.example.com", &serverAddr);

    // Wait for all requests to complete
    while (!context.allRequestsComplete) {
        // In a real application, you'd use proper event handling
        Sleep(100);
    }

    // Cleanup
    MsH3ConnectionClose(connection);
    MsH3ConfigurationClose(config);
    MsH3ApiClose(api);

    printf("All API requests completed!\n");
    return 0;
}
#endif
```

## Performance Considerations

### Benefits of Dynamic QPACK

1. **Reduced Bandwidth**: Repeated headers are compressed more efficiently
2. **Better Cache Utilization**: Similar headers across requests share table entries
3. **Scalability**: More efficient for applications with many similar requests

### Trade-offs

1. **Initial Latency**: First few requests may have slightly higher latency
2. **Memory Usage**: Dynamic table requires additional memory (4KB by default)
3. **Complexity**: More complex state management between encoder and decoder

### Best Practices

1. **Enable for API Clients**: REST API clients benefit significantly from dynamic QPACK
2. **Consider Request Patterns**: Most beneficial when headers have repeated patterns
3. **Monitor Performance**: Measure both compression efficiency and latency impact
4. **Test Thoroughly**: Ensure compatibility with your specific use case

## Debugging

To debug dynamic QPACK behavior, you can:

1. Enable verbose logging in MSH3 (if available)
2. Monitor network traffic to observe compression efficiency
3. Use the provided test case as a reference implementation

## Migration from Compile-Time Configuration

If you were previously using the `MSH3_DYNAMIC_QPACK` compile-time define, you should:

1. Remove the `#define MSH3_DYNAMIC_QPACK 1` from your build
2. Enable `MSH3_API_ENABLE_PREVIEW_FEATURES` in your build
3. Use the runtime configuration as shown in this document

The runtime configuration provides more flexibility and better integration with your application's configuration system.
