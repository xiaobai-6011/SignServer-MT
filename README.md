# Linux SignServer-MT

Multi-threaded signature server for NTQQ based on libsymbols.so.

## Supported QQ Versions

| Internal | Full Version |
|----------|--------------|
| 12912 | 3.1.2-12912 |
| 13107 | 3.1.2-13107 |
| 23361 | 3.2.7-23361 |
| 24815 | 3.2.9-24815 |
| 25765 | 3.2.10-25765 |
| 39038 | 3.2.19-39038 |
| 40990 | 3.2.20-40990 |

## Build

```bash
make
```

## Run

```bash
# default port: 11479
./sign_mt

# or run in background
nohup ./sign_mt &
```

## Lagrange-QQ Configuration

Modify `cmd/server/main.go`:

```go
qqClient.AddSignServer("http://127.0.0.1:11479/api/sign/39038")
```

## Architecture

```
Lagrange-QQ (Seoul)
    ↓ HTTP Sign Request
Local SignServer-MT (Seoul)
    ↓ Sign Response
Lagrange-QQ (Use Sign to Login/Send Messages)
```

## Important

### ⚠️ .gitignore Required

The following files **MUST NOT** be committed to Git:

```
*.so
*.node
*.o
sign_mt
nohup.out
```

## License

AGPLv3 - See LICENSE file
