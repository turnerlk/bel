# Guia de Instalação - VS Code + ESP32

Este guia mostra como configurar o ambiente para rodar o projeto ESP32 em `C:\Users\lulu2\Desktop\subirgit`.

## 1) Abrir o projeto no VS Code

1. Abra o VS Code.
2. Selecione `File > Open Folder...`.
3. Abra a pasta ``.

## 2) Instalar as extensões necessárias

Instale as seguintes extensões no VS Code:

- `PlatformIO IDE`
- `C/C++` (Microsoft)
- `Python` (Microsoft)

## 3) Instalar o PlatformIO

Se ainda não tiver o PlatformIO CLI instalado, use este comando no PowerShell:

```powershell
pip install platformio
```

No VS Code, a extensão `PlatformIO IDE` também oferece o ambiente integrado.

## 4) Verificar `platformio.ini`

Na raiz do projeto, confirme que existe o arquivo `platformio.ini`.

O conteúdo deve indicar um ambiente ESP32, por exemplo:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

## 5) Compilar o projeto

Abra o terminal integrado do VS Code (`Ctrl+`

Execute:

```powershell
platformio run
```

Isso fará o PlatformIO baixar dependências e compilar o firmware.

## 6) Enviar para o ESP32

Conecte o ESP32 via USB e identifique a porta serial no Windows.

Execute:

```powershell
platformio run --target upload
```

Se precisar forçar a porta manualmente:

```powershell
platformio run --target upload -e esp32dev -p COM5
```

## 7) Monitor serial

Para ver a saída do ESP32:

```powershell
platformio device monitor
```

Ou use a opção `PlatformIO: Monitor` pelo menu do VS Code.

## 8) Configurar repositório Git

Se quiser subir para o GitHub:

```powershell
cd C:\Users\lulu2\Desktop\subirgit
git init
git add .
git commit -m "Initial import"
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/SEU_REPO.git
git push -u origin main
```

## 9) O que está neste projeto

- `src/` — código fonte do ESP32
- `include/` — headers do projeto
- `platformio.ini` — configurações do projeto
- `README.md` — instruções básicas
- `README_INSTALL.md` — guia detalhado de instalação
- `.gitignore` — arquivos ignorados para PlatformIO

## 10) Dicas importantes

- Se o `platformio run` falhar, leia a mensagem de erro e instale o pacote indicado.
- Use a extensão `PlatformIO IDE` para compilar e fazer upload sem sair do VS Code.
- Mantenha o arquivo `platformio.ini` na raiz do projeto.

---

Assim você terá um projeto pronto para ser enviado ao ESP32 e também fácil de subir ao GitHub.
