# PlatformIO ESP32 Project

Projeto de exemplo para começar a usar PlatformIO com ESP32 e LVGL.

## Como usar

1. Abra a pasta `platformio_esp32_app` no VS Code.
2. Instale a extensão `PlatformIO IDE` se ainda não estiver instalada.
3. No terminal do VS Code, execute:

```powershell
platformio run
```

4. Para gravar no ESP32, use:

```powershell
platformio run --target upload
```

## Observações

- Este projeto está configurado para `board = esp32dev`.
- A biblioteca LVGL é referenciada via `lib_extra_dirs = ../lvgl-master`.
- Se você quiser usar o LVGL do PlatformIO Registry, remova `lib_extra_dirs` e adicione `lib_deps = lvgl/lvgl@^9.0.0`.
