# 펌웨어 설치와 첫 설정 (AS-008)

보드에 들어가는 것은 두 가지입니다. **펌웨어**(앱)와 **웹 UI**(SPIFFS의 `index.html` 등)입니다. 웹 UI가 없으면 Wi-Fi 설정 화면이 404로 뜨므로, 처음에는 반드시 둘 다 씁니다. 아래 세 방법 모두 둘을 함께 씁니다.

## 다운로드 모드 들어가기 (공통)

CH340N의 DTR/RTS가 ESP32에 연결되어 있지 않아 컴퓨터가 보드를 리셋할 수 없습니다.

1. 보드의 버튼을 **누른 채** USB-C를 꽂고, 1초 뒤 버튼을 놓습니다.
2. 굽기가 끝나면 USB를 뽑았다가 **버튼 없이** 다시 꽂아야 새 펌웨어가 시작됩니다.

macOS는 CH340 드라이버가 내장되어 있어 `/dev/cu.usbserial-*` 또는 `/dev/cu.wchusbserial*`로 보입니다. 아무것도 안 보이면 충전 전용 케이블인지 먼저 확인하세요.

## 방법 1. 명령 한 줄 (이 저장소가 있는 Mac)

```sh
sh tools/flash.sh
```

빌드 → 웹 UI 이미지 생성 → 포트 대기("버튼 누른 채 꽂으세요" 안내) → 쓰기까지 한 번에 합니다. Wi-Fi·Spotify 설정은 지우지 않습니다.

| 명령 | 용도 |
|---|---|
| `sh tools/flash.sh` | 일반 설치/업데이트. 설정 유지 |
| `sh tools/flash.sh --erase` | 칩 전체를 지우고 설치(공장 초기화). 첫 설치나 이상할 때 |
| `sh tools/flash.sh --image` | 굽지 않고 `work/flash/`에 파일만 생성 |
| `PORT=/dev/cu.usbserial-110 sh tools/flash.sh` | 포트를 직접 지정 |

`.venv`가 없으면 README의 "빌드와 플래시" 절대로 먼저 만듭니다.

## 방법 2. 브라우저로 굽기 (설치 없이, Chrome/Edge)

빌드된 파일 하나만 있으면 어떤 컴퓨터에서도 됩니다.

1. 파일 만들기: `sh tools/flash.sh --image` → `work/flash/lovelace-<버전>-full.bin` (8MB, 부트로더 + 파티션 + 앱 + 웹 UI가 합쳐진 이미지).
2. Chrome 또는 Edge로 Espressif 공식 웹 플래셔 <https://espressif.github.io/esptool-js/> 를 엽니다(Safari·Firefox는 Web Serial 미지원).
3. 버튼 누른 채 USB 연결 → **Baudrate 460800** → **Connect** → 목록에서 USB Serial(CH340) 선택.
4. **Flash Address `0x0`**, 파일에 `lovelace-<버전>-full.bin` 선택 → **Program**.
5. 끝나면 USB를 뽑았다 버튼 없이 다시 꽂습니다. 웹 페이지의 Reset 버튼은 이 보드에서 동작하지 않습니다.

이 합친 이미지는 빈 영역을 `0xFF`로 채우므로 **NVS(Wi-Fi, Spotify 로그인, BT 페어링)도 지워집니다.** 설정을 유지하며 업데이트하려면 방법 1이나 3을 씁니다.

## 방법 3. 웹 UI OTA (두 번째부터, USB 불필요)

보드가 집 Wi-Fi에 붙은 뒤에는 `http://<보드 IP>/`의 펌웨어 업데이트 칸에 `work/flash/lovelace-<버전>-ota.bin`을 올리면 됩니다. 설정은 유지됩니다. OTA는 앱만 바꾸므로 `firmware/data/www`의 웹 UI를 바꾼 경우에는 방법 1을 씁니다.

## 첫 부팅 확인

```sh
.venv/bin/pio device monitor -d firmware
```

115200bps 로그에서 순서대로 봅니다: PSRAM 8MB, 플래시 8MB, `Mute GPIO 22 initialized`, `I2S initialized`, `Boot baseline: free heap ...`(이 숫자를 적어 두세요), `Connect to 'LOVELACE-Setup' -> http://192.168.4.1`.

## 내가 해야 할 일 (첫 설정)

1. **Wi-Fi**: 휴대폰/Mac으로 비밀번호 없는 `LOVELACE-Setup`에 접속 → `http://192.168.4.1/` → 집 **2.4GHz** Wi-Fi 입력 → 재부팅. 게스트망·기기 격리·mDNS 차단이 있으면 AirPlay/Spotify가 안 보입니다.
2. **AirPlay**: iPhone/Mac 제어 센터에서 `LOVELACE` 선택. 별도 계정·토큰이 필요 없습니다.
3. **Bluetooth**: 휴대폰 Bluetooth 목록에서 `LOVELACE`. 다른 폰을 붙이려면 버튼 3초.
4. **Spotify Connect** (Premium 필요):
   1. <https://developer.spotify.com/dashboard> 에서 앱을 하나 만들고(API는 Web API, Redirect URI는 쓰이지 않으므로 `http://127.0.0.1:8888/callback` 같은 값) Client ID / Client Secret을 복사합니다.
   2. `http://<보드 IP>/spotify`에 두 값을 저장하고 보드를 재부팅합니다.
   3. Spotify 앱 → 기기 선택 → `LOVELACE` → 재생. 이때 앱이 보드에 계정 로그인 정보를 넘깁니다(비밀번호는 보드에 입력하지 않음).
   4. 같은 `/spotify` 화면의 상태 JSON에서 `state`와 `last_error`를 확인합니다(아래 표).

## Spotify 토큰과 알려진 위험

보드는 Spotify에 세 가지 인증을 씁니다.

| 단계 | 무엇 | 실패하면 `last_error` |
|---|---|---|
| ① 계정 로그인 | 앱이 넘긴 Zeroconf 블롭 → Spotify AP 세션, 재사용 로그인을 NVS에 저장 | `Spotify login rejected` → 저장된 로그인을 지우고 앱에서 다시 선택 대기 |
| ② 접근 토큰 | 개발자 앱 Client ID/Secret으로 `accounts.spotify.com/api/token` (client credentials, 약 1시간마다 갱신) | `Spotify token rejected; check developer app credentials` → ID/Secret 오타, 앱 삭제. 30초마다 재시도 |
| ③ 곡 다운로드 | ②의 토큰으로 storage-resolve → CDN 주소, AP 세션으로 곡별 오디오 키 | `Spotify storage-resolve failed (token or network)` 또는 `Spotify refused the audio key (code ....)` |

**③은 보드에서 실제로 확인해야 합니다.** 2025년 말부터 Spotify가 librespot 계열(cspot 포함) 비공식 클라이언트에 오디오 키를 거부하는 사례가 많습니다. librespot README는 일정 시점 이후 만든 계정에서 `audio key error 0 1`이 나고 알려진 우회 방법이 없다고 적고 있습니다([librespot#1649](https://github.com/librespot-org/librespot/issues/1649)). 이 보드에서 `Spotify refused the audio key (code 0001)`이 나오면 펌웨어 문제가 아니라 같은 현상입니다. 또 storage-resolve에 계정 토큰이 아닌 앱 토큰(client credentials)을 쓰는 것은 이 포크의 선택이라, 거부되면 `storage-resolve failed`로 나옵니다. 두 경우 모두 AirPlay와 Bluetooth에는 영향이 없습니다. 어느 메시지가 나왔는지 알려 주면 다음 대응을 정합니다.

0.3.2 이전 펌웨어는 ②가 실패하면(잘못된 Secret, 인터넷 끊김) 예외가 잡히지 않아 보드가 재부팅을 반복했습니다. 0.3.2부터는 오류를 기록하고 30초 뒤 다시 시도합니다.
