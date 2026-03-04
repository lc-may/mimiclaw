# MimiClaw

MimiClaw は現在 Linux / WSL 専用のネイティブ C プロジェクトです。ESP32 / ESP-IDF 向けの経路はこのリポジトリから削除されています。

まず `mimi_secrets.h` を作成します。これはビルド時マクロだからです:

```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

その後にビルド:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libcjson-dev libwebsockets-dev

cmake -S . -B build
cmake --build build -j
```

最後に実行:

```bash
./build/mimiclaw
```

詳細は [README.md](README.md) を参照してください。
