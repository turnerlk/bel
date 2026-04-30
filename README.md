# Projeto ESP32 - subirgit

Este diretório contém o projeto `MINITELA7` que já está configurado para ESP32 com PlatformIO.

## Estrutura principal

- `platformio.ini` - configuração do projeto PlatformIO
- `src/` - código fonte do ESP32
- `include/` - headers e arquivos adicionais
- `lib/` - bibliotecas locais (se houver)

## Passo a passo para configurar no VS Code

1. Abra o VS Code.
2. Abra a pasta `C:\Users\lulu2\Desktop\subirgit` no VS Code.
3. Instale a extensão `PlatformIO IDE` se não estiver instalada.
4. Abra a paleta de comandos (`Ctrl+Shift+P`) e execute `PlatformIO: Initialize or Update PlatformIO Project` se for solicitado.
5. Verifique se o arquivo `platformio.ini` está presente na raiz.
6. Abra o terminal integrado do VS Code (`Ctrl+`
7. Rode:

```powershell
platformio run
```

8. Para enviar ao ESP32:

```powershell
platformio run --target upload
```

9. Para abrir o monitor serial:

```powershell
platformio device monitor
```

## Como instalar as dependências do ambiente ESP32

### Instalar o PlatformIO

- Abra o VS Code.
- Instale a extensão `PlatformIO IDE`.
- Caso precise, instale o PlatformIO CLI global com Python:

```powershell
pip install platformio
```

### Instalar o toolchain ESP32

O PlatformIO deve baixar automaticamente os pacotes necessários ao compilar o projeto pela primeira vez.

Se necessário, confirme no `platformio.ini` qual plataforma está sendo usada:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

### Conectar o ESP32

1. Conecte o ESP32 ao USB.
2. Abra o monitor serial para ver as mensagens de boot.
3. Selecione a porta COM correta.

## Observações

- Este projeto usa `PlatformIO` e não `ESP-IDF` diretamente.
- Se quiser subir para o GitHub, basta iniciar um repositório dentro desta pasta e commitar os arquivos.
- Não esqueça de adicionar `.gitignore` se quiser excluir arquivos binários ou de build.
