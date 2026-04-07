# Linux SignServer-MT

基于 libsymbols.so 的 NTQQ 签名服务，支持多线程并发处理。

## 支持的 QQ 版本

| 内部版本 | 完整版本 |
|----------|----------|
| 12912 | 3.1.2-12912 |
| 13107 | 3.1.2-13107 |
| 23361 | 3.2.7-23361 |
| 24815 | 3.2.9-24815 |
| 25765 | 3.2.10-25765 |
| 39038 | 3.2.19-39038 |
| 40990 | 3.2.20-40990 |

## 编译

```bash
make
```

## 运行

```bash
# 默认端口: 11479
./sign_mt

# 或后台运行
nohup ./sign_mt &
```

## Lagrange-QQ 配置

修改 `cmd/server/main.go`:

```go
qqClient.AddSignServer("http://127.0.0.1:11479/api/sign/39038")
```

## 架构

```
Lagrange-QQ (首尔)
    ↓ HTTP 签名请求
本地 SignServer-MT (首尔)
    ↓ 签名响应
Lagrange-QQ (使用签名登录/发送消息)
```

## 重要提醒

### ⚠️ 必须配置 .gitignore

以下文件**禁止提交到 Git**：

```
*.so
*.node
*.o
sign_mt
nohup.out
```

## 许可证

AGPLv3 - 见 LICENSE 文件
