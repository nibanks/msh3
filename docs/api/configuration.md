# Configuration API

The Configuration API is used to create and manage configuration settings for MSH3 connections.

## MsH3ConfigurationOpen

```c
MSH3_CONFIGURATION*
MSH3_CALL
MsH3ConfigurationOpen(
    MSH3_API* Api,
    const MSH3_SETTINGS* Settings, // optional
    uint32_t SettingsLength
    );
```

Creates a new configuration object.

### Parameters

`Api` - The API object handle.

`Settings` - Optional pointer to a structure containing settings for the configuration.

`SettingsLength` - The size of the settings structure in bytes.

### Returns

Returns a pointer to the new configuration object, or NULL if the operation fails.

### Remarks

The configuration object is used to configure connections. The settings parameter is optional and can be NULL.

### Example

```c
MSH3_SETTINGS settings = {0};
settings.IsSetFlags = 0;
settings.IsSet.IdleTimeoutMs = 1;
settings.IdleTimeoutMs = 30000;  // 30 seconds

MSH3_CONFIGURATION* config = 
    MsH3ConfigurationOpen(api, &settings, sizeof(settings));
if (config == NULL) {
    printf("Failed to create configuration\n");
    return 1;
}
```

## MsH3ConfigurationLoadCredential

```c
MSH3_STATUS
MSH3_CALL
MsH3ConfigurationLoadCredential(
    MSH3_CONFIGURATION* Configuration,
    const MSH3_CREDENTIAL_CONFIG* CredentialConfig
    );
```

Loads credentials into a configuration object.

### Parameters

`Configuration` - The configuration object.

`CredentialConfig` - The credential configuration to load.

### Returns

Returns MSH3_STATUS_SUCCESS if successful, or an error code otherwise.

### Remarks

This function is used to load security credentials for TLS. Different credential types are supported through the MSH3_CREDENTIAL_CONFIG structure.

### Example

```c
MSH3_CERTIFICATE_FILE certFile = {
    .PrivateKeyFile = "client.key",
    .CertificateFile = "client.crt"
};

MSH3_CREDENTIAL_CONFIG credConfig = {
    .Type = MSH3_CREDENTIAL_TYPE_CERTIFICATE_FILE,
    .Flags = MSH3_CREDENTIAL_FLAG_CLIENT,
    .CertificateFile = &certFile
};

MSH3_STATUS status = 
    MsH3ConfigurationLoadCredential(config, &credConfig);
if (MSH3_FAILED(status)) {
    printf("Failed to load credentials, status=0x%x\n", status);
    return 1;
}
```

## MsH3ConfigurationClose

```c
void
MSH3_CALL
MsH3ConfigurationClose(
    MSH3_CONFIGURATION* Configuration
    );
```

Closes a configuration object.

### Parameters

`Configuration` - The configuration object to close.

### Remarks

This function should be called when the configuration is no longer needed. After calling this function, the configuration handle is no longer valid.

### Example

```c
MSH3_CONFIGURATION* config = MsH3ConfigurationOpen(api, NULL, 0);
// Use configuration...
MsH3ConfigurationClose(config);
```
