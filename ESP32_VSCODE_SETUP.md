# Setup ESP32 + VS Code

Este projeto principal é o `lvgl-master`, que contém a biblioteca LVGL. Para compilar no ESP32 você deve usar o ESP-IDF como framework e criar um projeto de aplicação que consome esta biblioteca como componente.

## 1) O que precisa

- Windows 10/11
- Git
- Python 3.x
- CMake
- Ninja
- ESP-IDF (instalado via ESP-IDF Tools Installer)
- VS Code
- Extensão **ESP-IDF** para VS Code

## 2) O que este repositório tem

- `lvgl-master/` é a biblioteca LVGL
- `PASSO_A_PASSO_SETUP.md` já contém instruções de setup
- Este arquivo adiciona um passo a passo específico para VS Code

## 3) Instalar ferramentas no Windows

1. Instale Git, CMake, Ninja e Python:

```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
winget install --id Python.Python.3.12 -e
```

2. Baixe e instale o ESP-IDF Tools Installer oficial da Espressif.
3. Durante a instalação, marque:
   - ESP-IDF
   - Python environment
   - CMake e Ninja (se ainda não estiverem instalados)

## 4) Preparar o VS Code

1. Abra o VS Code.
2. Instale a extensão **ESP-IDF**.
3. Para PlatformIO, instale também a extensão **PlatformIO IDE**.
4. Opcional, mas recomendado:
   - instale `ms-vscode.cpptools`
   - instale `ms-vscode.cmake-tools`
   - instale `ms-python.python`
5. Abra a paleta de comandos (`Ctrl+Shift+P`) e execute:
   - `ESP-IDF: Configure ESP-IDF extension`
6. Se solicitado, informe o caminho do ESP-IDF instalado.

> Foi criada a recomendação de workspace em `.vscode/extensions.json` para facilitar a instalação dos plugins.

## 5) Exemplo PlatformIO para ESP32

Há um projeto de exemplo em `platformio_esp32_app`.

1. Abra a pasta `platformio_esp32_app` no VS Code.
2. Instale a extensão **PlatformIO IDE**.
3. Execute `PlatformIO: Build` ou, no terminal:

```powershell
platformio run
```

4. Para enviar ao ESP32:

```powershell
platformio run --target upload
```

O arquivo `platformio.ini` já está configurado para `board = esp32dev` e usa a pasta local `lvgl-master` como biblioteca.

## 6) Criar projeto ESP-IDF para rodar no ESP32

No terminal do Windows ou no **ESP-IDF PowerShell**:

```powershell
cd "C:\Users\lulu2\Downloads\projeto"
idf.py create-project lvgl_esp32_app
cd "C:\Users\lulu2\Downloads\projeto\lvgl_esp32_app"
```

Depois abra essa pasta no VS Code.

## 6) Adicionar LVGL ao projeto

Recomendado:

```powershell
idf.py add-dependency "espressif/esp_lvgl_port^2.3.0"
```

Alternativa:

```powershell
idf.py add-dependency "lvgl/lvgl^9.*"
```

Se você quiser usar diretamente o código de `lvgl-master`, crie um componente local ou adicione o repositório como submódulo/componente.

## 7) Configurar target ESP32

No terminal do projeto ou no VS Code:

```powershell
idf.py set-target esp32
idf.py menuconfig
```

Ajuste no `menuconfig`:
- porta serial correta
- flash size/frequency
- configurações de LCD/Touch se o projeto usar display

## 8) Compilar no VS Code

No terminal integrado do VS Code, execute:

```powershell
idf.py build
```

Se preferir usar o botão de build da extensão ESP-IDF, abra a paleta e escolha `ESP-IDF: Build`.

## 9) Gravar no ESP32

Identifique a porta COM do seu ESP32 (por exemplo `COM5`) e rode:

```powershell
idf.py -p COM5 flash
```

## 10) Monitorar saída serial

```powershell
idf.py -p COM5 monitor
```

Ou use a opção `ESP-IDF: Open Serial Monitor` no VS Code.

## 11) Dicas importantes

- Se `idf.py` não for reconhecido, abra o terminal **ESP-IDF PowerShell** ou use o terminal integrado do VS Code que já carregou o ambiente ESP-IDF.
- O repositório `lvgl-master` é a biblioteca; não é um projeto ESP-IDF pronto para flashar sozinho.
- Para colocar no repo, mantenha este arquivo na raiz junto com `PASSO_A_PASSO_SETUP.md`.

## 12) Exemplo de fluxo ideal

1. Instalar ESP-IDF e extensões.
2. Criar projeto ESP-IDF.
3. Adicionar dependência LVGL/ESP.
4. Abrir projeto no VS Code.
5. Build, flash e monitor.

---

Este documento serve como guia para configurar o ambiente VS Code e compilar no ESP32 usando ESP-IDF.
