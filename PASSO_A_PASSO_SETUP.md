# Passo a passo de instalação e criação do repositório

Este projeto é o `lvgl-master` (C/C++), então o setup principal usa Git, CMake, Ninja e Python.

## 1) Instalar ferramentas essenciais (Windows)

Abra o PowerShell como Administrador e execute:

```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
winget install --id Python.Python.3.12 -e
```

Se o `winget` nao estiver disponivel, instale manualmente pelos sites oficiais.

## 2) Validar instalacao

Feche e abra o PowerShell novamente, depois rode:

```powershell
git --version
cmake --version
ninja --version
python --version
```

## 3) Criar repositorio Git local

Entre na pasta do projeto:

```powershell
cd "C:\Users\lulu2\Downloads\projeto\lvgl-master"
git init
git add .
git commit -m "chore: initial import of lvgl project"
```

## 4) Criar e conectar repositorio remoto (GitHub)

1. Crie um repositorio vazio no GitHub (sem README).
2. Execute:

```powershell
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/NOME_DO_REPO.git
git push -u origin main
```

## 5) Instalacoes opcionais (somente se precisar)

### Gerar documentacao

```powershell
cd "C:\Users\lulu2\Downloads\projeto\lvgl-master"
pip install -r docs/requirements.txt
```

### Scripts Python auxiliares

```powershell
cd "C:\Users\lulu2\Downloads\projeto\lvgl-master"
pip install -r scripts/gdb/requirements.txt
pip install -r scripts/gen_json/requirements.txt
```

## Observacoes

- O projeto principal nao usa Node.js por padrao.
- Para colaboradores, vale adicionar este arquivo ao repositorio para padronizar o setup.

## 6) ESP32 (ESP-IDF): compilar, gravar e monitorar

Se voce quer rodar LVGL no ESP32, o caminho recomendado e usar ESP-IDF com `esp_lvgl_port`.

### 6.1 Instalar ESP-IDF no Windows

1. Baixe o **ESP-IDF Tools Installer** oficial da Espressif (Windows).
2. Durante a instalacao, mantenha marcado:
   - ESP-IDF
   - Python environment
   - CMake e Ninja (se ainda nao estiverem instalados)
3. Abra o terminal **ESP-IDF PowerShell** (atalho criado pelo instalador).

### 6.2 Criar um projeto base ESP-IDF

No terminal ESP-IDF:

```powershell
cd "C:\Users\lulu2\Downloads\projeto"
idf.py create-project lvgl_esp32_app
cd "C:\Users\lulu2\Downloads\projeto\lvgl_esp32_app"
```

### 6.3 Adicionar LVGL ao projeto

Opcao recomendada (porta oficial para ESP):

```powershell
idf.py add-dependency "espressif/esp_lvgl_port^2.3.0"
```

Opcao alternativa (LVGL direto, sem o port):

```powershell
idf.py add-dependency "lvgl/lvgl^9.*"
```

### 6.4 Configurar target e parametros

```powershell
idf.py set-target esp32
idf.py menuconfig
```

No `menuconfig`, ajuste:
- porta serial (se necessario),
- flash size/frequency,
- e configuracoes de componentes LVGL (quando disponiveis).

### 6.5 Compilar

```powershell
idf.py build
```

### 6.6 Gravar no ESP32

Descubra a porta COM no Gerenciador de Dispositivos (exemplo `COM5`) e rode:

```powershell
idf.py -p COM5 flash
```

### 6.7 Abrir monitor serial

```powershell
idf.py -p COM5 monitor
```

Atalho util para tudo de uma vez:

```powershell
idf.py -p COM5 build flash monitor
```

### 6.8 Estrutura minima para integrar seu codigo LVGL

- Coloque sua inicializacao no `app_main()`.
- Inicie o subsistema LVGL via `bsp_display_start()` (quando usar `esp_bsp`) ou `lvgl_port_init()` (quando configurar manualmente).
- Adicione driver de display/toque compativel com sua tela (por exemplo componentes `esp_lcd_*`).

### 6.9 Erros comuns

- **`idf.py` nao reconhecido**: voce abriu PowerShell comum; use o **ESP-IDF PowerShell**.
- **Falha de porta serial**: confirme COM correta e feche programas que estejam usando a porta.
- **Falha de dependencia**: execute `idf.py reconfigure` e depois `idf.py build`.
