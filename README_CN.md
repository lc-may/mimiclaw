# MimiClaw

MimiClaw 现在是一个仅支持 Linux / WSL 的本地 C 项目，ESP32 / ESP-IDF 版本已经从仓库中移除。

先复制 `mimi_secrets.h`，因为这里的配置是编译期宏：

```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

再构建：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libcjson-dev libwebsockets-dev

cmake -S . -B build
cmake --build build -j
```

最后运行：

```bash
./build/mimiclaw
```

更完整的说明见 [README.md](README.md)。
