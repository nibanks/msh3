# Global API

The MSH3 global API provides functions for initializing and cleaning up the MSH3 library.

## MsH3Version

```c
void
MSH3_CALL
MsH3Version(
    uint32_t Version[4]
    );
```

Gets the MSH3 library version.

### Parameters

`Version` - An array of 4 uint32_t values to receive the version information.

### Remarks

The version array is filled with major, minor, patch, and build numbers.

### Example

```c
uint32_t version[4];
MsH3Version(version);
printf("MSH3 version: %u.%u.%u.%u\n", version[0], version[1], version[2], version[3]);
```

## MsH3ApiOpen

```c
MSH3_API*
MSH3_CALL
MsH3ApiOpen(
    void
    );
```

Creates and initializes a new API object.

### Returns

Returns a pointer to the newly created API object, or NULL if the operation fails.

### Remarks

This function must be called before using any other MSH3 functions. The returned handle is used by other API calls.

### Example

```c
MSH3_API* api = MsH3ApiOpen();
if (api == NULL) {
    printf("Failed to initialize MSH3 API\n");
    return 1;
}
```

## MsH3ApiClose

```c
void
MSH3_CALL
MsH3ApiClose(
    MSH3_API* Api
    );
```

Cleans up and closes an API object.

### Parameters

`Api` - The API object to close.

### Remarks

This function should be called when you're done using MSH3 to clean up resources. After calling this function, the API handle is no longer valid.

### Example

```c
MSH3_API* api = MsH3ApiOpen();
// Use MSH3 API...
MsH3ApiClose(api);
```
