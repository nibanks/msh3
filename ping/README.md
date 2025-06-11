# h3ping - HTTP/3 Connectivity Testing Tool

h3ping is a network testing tool for HTTP/3 connectivity, similar to the traditional `ping` utility but for HTTP/3 over QUIC. It tests reachability and measures round-trip time for HTTP/3 connections.

## Features

- **HTTP/3 connectivity testing** - Tests if a server supports HTTP/3
- **Latency measurement** - Measures round-trip time for HTTP/3 requests
- **Ping-like statistics** - Shows packet loss, min/avg/max latency
- **Customizable options** - Control request count, interval, timeout
- **Verbose mode** - Detailed output for debugging
- **Unsecure mode** - Skip certificate validation for testing

## Usage

```bash
h3ping <server[:port]> [options...]
```

### Options

- `-c, --count <num>` - Number of requests to send (default=4, 0=infinite)
- `-g, --get` - Use GET requests instead of HEAD (default=HEAD)
- `-h, --help` - Print help text
- `-i, --interval <ms>` - Interval between requests in milliseconds (default=1000)
- `-p, --path <path>` - Path to request (default=/)
- `-t, --timeout <ms>` - Timeout for each request in milliseconds (default=5000)
- `-u, --unsecure` - Allow unsecure connections (skip certificate validation)
- `-v, --verbose` - Enable verbose output
- `-V, --version` - Print version information

### Examples

```bash
# Basic HTTP/3 ping
h3ping www.google.com

# Ping with custom count and interval
h3ping www.cloudflare.com -c 10 -i 500

# Test specific path with GET requests
h3ping nghttp2.org:4433 -p /httpbin/get -g

# Infinite ping mode (press Ctrl+C to stop)
h3ping outlook.office.com -c 0

# Verbose mode with unsecure connections
h3ping localhost:4433 -v -u
```

## Output

h3ping produces output similar to traditional ping:

```
HTTP/3 pinging www.google.com:443 [ip] with HEAD:
Response from www.google.com: time=45.123ms
Response from www.google.com: time=32.456ms
Response from www.google.com: time=28.789ms
Response from www.google.com: time=31.012ms

--- www.google.com HTTP/3 ping statistics ---
4 requests transmitted, 4 received, 0.0% packet loss
round-trip min/avg/max = 28/34/45 ms
```

## How it Works

h3ping establishes an HTTP/3 connection to the target server and sends HTTP HEAD requests (or GET requests with -g option) to test connectivity. It measures the time from request initiation to response completion and provides statistics similar to the traditional ping utility.

The tool uses the msh3 HTTP/3 library, which builds on top of Microsoft's MsQuic implementation for QUIC transport.

## Building

h3ping is built as part of the msh3 project:

```bash
# Configure with ping tool enabled
cmake -DMSH3_PING=ON ..

# Build
make h3ping
```