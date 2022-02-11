# msh3

Minimal HTTP/3 client on top of [microsoft/msquic](https://github.com/microsoft/msquic) and [litespeedtech/ls-qpack](https://github.com/litespeedtech/ls-qpack).

# Build

```
git clone https://github.com/nibanks/msh3
git submodule update --init --recursive
...
```

# Run

```
msh3 -server:cloudflare.com
```
