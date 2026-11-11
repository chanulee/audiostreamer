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
sh tools/monitor.sh
```

`pio device monitor`를 직접 쓰려면 먼저 `export PLATFORMIO_CORE_DIR="$PWD/work/platformio"`가 필요합니다. 없으면 `Unknown development platform` 오류가 납니다.

115200bps 로그에서 순서대로 봅니다: PSRAM 8MB, 플래시 8MB, `Mute GPIO 22 initialized`, `I2S initialized`, `Boot baseline: free heap ...`(이 숫자를 적어 두세요), `Connect to 'LOVELACE-Setup' -> http://192.168.4.1`.

## 내가 해야 할 일 (첫 설정)

1. **Wi-Fi**: 휴대폰/Mac으로 비밀번호 없는 `LOVELACE-Setup`에 접속 → `http://192.168.4.1/` → 집 **2.4GHz** Wi-Fi 입력 → Connect. 보드는 재부팅하지 않고 그 자리에서 접속을 시도하며, 화면에 결과가 나옵니다.
   - 성공: 집 Wi-Fi의 IP와 `http://lovelace.local/` 주소가 표시되고, LED가 네 번 빠르게 깜빡인 뒤 2.5초마다 한 번 깜빡입니다. `LOVELACE-Setup`은 2분 뒤 꺼집니다. **이후 웹 UI(LED 밝기, Spotify, OTA 등)는 집 Wi-Fi에서 `http://lovelace.local/`로 엽니다.** 설정용 Wi-Fi는 열어 두지 않습니다(누구나 접속할 수 있고, Wi-Fi 송신 시간을 나눠 써서 오디오가 끊기기 쉬워짐).
   - 실패: 비밀번호 틀림 / 네트워크 못 찾음 같은 이유가 표시되고, LED가 세 번씩 두 차례 깜빡인 뒤 두 번 깜빡임(설정 대기)으로 돌아갑니다. 저장하지 않으므로 다시 입력하면 됩니다.
   - 집 Wi-Fi에 붙을 때 보드의 무선 채널이 바뀌어 Mac이 `LOVELACE-Setup`에서 잠깐 끊길 수 있습니다. 결과 화면이 사라졌다면 LED로 판단하세요.
   - 집 Wi-Fi가 바뀌거나 계속 실패하면(5회) `LOVELACE-Setup`이 자동으로 다시 열립니다.
   - 게스트망·기기 격리·mDNS 차단이 있으면 AirPlay/Spotify와 `lovelace.local`이 안 보입니다. 그때는 공유기 관리 화면에서 `LOVELACE`의 IP를 찾아 직접 엽니다.
   - **`lovelace.local`만 안 열리고 IP는 열리면** 공유기/통신사 DNS가 `.local` 이름에 공인 IP를 대신 답하는 경우입니다(2026-11-11 SK 공유기에서 확인: `218.38.137.27`로 응답). AirPlay·Spotify 검색에는 영향이 없습니다. 성공 화면의 IP를 북마크하고, 공유기의 DHCP 고정 할당(주소 예약)에 보드 MAC을 등록하면 IP가 바뀌지 않습니다. 확인 명령: `dscacheutil -q host -a name lovelace.local`
2. **AirPlay**: iPhone/Mac 제어 센터에서 `LOVELACE` 선택. 별도 계정·토큰이 필요 없습니다.
3. **Bluetooth**: 휴대폰 Bluetooth 목록에서 `LOVELACE`. 다른 폰을 붙이려면 버튼 3초.
4. **Spotify Connect** (Premium 필요, 0.3.9부터 **기본 꺼짐**): `http://<보드 IP>/spotify`에서 **Spotify Connect 켜기** → 재부팅한 뒤 아래를 진행합니다. 계정에 따라 재생이 막힐 수 있고, 켜 두면 AirPlay 연결이 실패할 수 있습니다(아래 "알려진 위험").
   1. <https://developer.spotify.com/dashboard> 에서 앱을 하나 만들고(API는 Web API, Redirect URI는 쓰이지 않으므로 `http://127.0.0.1:8888/callback` 같은 값) Client ID / Client Secret을 복사합니다.
   2. `http://<보드 IP>/spotify`에 두 값을 저장하고 보드를 재부팅합니다.
   3. Spotify 앱 → 기기 선택 → `LOVELACE` → 재생. 이때 앱이 보드에 계정 로그인 정보를 넘깁니다(비밀번호는 보드에 입력하지 않음).
   4. 같은 `/spotify` 화면의 상태 JSON에서 `state`와 `last_error`를 확인합니다(아래 표).

## LED 읽는 법

초록 LED 하나로 상태를 보여 줍니다. 폰이 연결되어 있으면 재생 상태가, 아니면 네트워크 상태가 보입니다.

| LED | 뜻 |
|---|---|
| 빠르게 파르르 (0.2초 간격) | 부팅 중, Wi-Fi 연결 시도 중 |
| 두 번 깜빡, 2초 쉼 반복 | 설정 필요: `LOVELACE-Setup`에 접속해 Wi-Fi를 입력하세요 |
| 2.5초마다 짧게 한 번 | 준비 완료(집 Wi-Fi 연결, 대기 중) |
| 1초 주기로 깜빡 | 폰이 연결됨, 일시정지 |
| 계속 켜짐 | 재생 중 |
| 0.5초 주기로 깜빡 | Bluetooth 페어링 대기(버튼 3초). 폰이 연결되거나 60초가 지나면 끝 |
| 세 번 깜빡, 쉼 반복 | 오류 |
| 네 번 빠르게 (한 번만) | 성공: Wi-Fi 연결 완료, Bluetooth 폰 연결 |
| 잠깐 두 번 꺼짐 (한 번만) | 버튼 입력을 받음 |
| 세 번씩 두 차례 (한 번만) | Wi-Fi 연결 실패 |

## 상태 숫자로 확인 (`/api/system/info`)

`curl http://<보드 IP>/api/system/info` 또는 웹 UI의 System 칸.

| 항목 | 뜻 |
|---|---|
| `source` | 지금 출력을 가진 소스(`idle`/`airplay`/`bluetooth`/`spotify`) |
| `dram_free`, `dram_largest` | 태스크 스택이 쓸 수 있는 내부 RAM. 0.3.5 기준 대기 중 약 24KB. 몇 KB까지 떨어지면 AirPlay/HTTP 연결이 실패합니다 |
| `bt_sample_rate` | A2DP 샘플레이트(44100/48000). I2S 클럭이 그대로 따라갑니다(리샘플링 없음) |
| `bt_latency_ms` | 보드 안 지연(FIFO 프리필 + I2S DMA + 한 프레임). 휴대폰에 지연 보고로도 전달 |
| `bt_buffer_ms` | 재생 중 FIFO에 쌓인 양. 시간이 지나며 계속 오르거나 내리면 휴대폰과 보드 클럭이 어긋나는 것 |
| `bt_underruns`, `bt_dropped` | 부팅 후 누적 끊김 횟수(버퍼 바닥남 / 가득 차서 버림). 재생 중 늘면 그 순간 소리가 끊긴 것 |

## Spotify 토큰과 알려진 위험

보드는 Spotify에 세 가지 인증을 씁니다.

| 단계 | 무엇 | 실패하면 `last_error` |
|---|---|---|
| ① 계정 로그인 | 앱이 넘긴 Zeroconf 블롭 → Spotify AP 세션, 재사용 로그인을 NVS에 저장 | `Spotify login rejected` → 저장된 로그인을 지우고 앱에서 다시 선택 대기 |
| ② 접근 토큰 | 개발자 앱 Client ID/Secret으로 `accounts.spotify.com/api/token` (client credentials, 약 1시간마다 갱신) | `Spotify token rejected; check developer app credentials` → ID/Secret 오타, 앱 삭제. 30초마다 재시도 |
| ③ 곡 다운로드 | ②의 토큰으로 storage-resolve → CDN 주소, AP 세션으로 곡별 오디오 키 | `Spotify storage-resolve failed (token or network)` 또는 `Spotify refused the audio key (code ....)` |

**③은 보드에서 실제로 확인해야 합니다.** 2025년 말부터 Spotify가 librespot 계열(cspot 포함) 비공식 클라이언트에 오디오 키를 거부하는 사례가 많습니다. librespot README는 일정 시점 이후 만든 계정에서 `audio key error 0 1`이 나고 알려진 우회 방법이 없다고 적고 있습니다([librespot#1649](https://github.com/librespot-org/librespot/issues/1649)). 이 보드에서 `Spotify refused the audio key (code 0001)`이 나오면 펌웨어 문제가 아니라 같은 현상입니다. 또 storage-resolve에 계정 토큰이 아닌 앱 토큰(client credentials)을 쓰는 것은 이 포크의 선택이라, 거부되면 `storage-resolve failed`로 나옵니다. 두 경우 모두 AirPlay와 Bluetooth에는 영향이 없습니다. 어느 메시지가 나왔는지 알려 주면 다음 대응을 정합니다.

**2026-11-11 실측(AS-008, 펌웨어 0.3.7):** 0.3.6까지는 ① 로그인이 `BadCredentials (12)`로 거절됐습니다. cspot이 보내던 아주 오래된 클라이언트 버전(`0x10800000000`)과 `OS_UNKNOWN`을 librespot 0.8과 같은 값(`124200290`, `LINUX/X86`)으로 바꾼 0.3.7에서 로그인은 성공했습니다. 그러나 곡 재생은 ③에서 `Spotify refused the audio key (code 0001)`로 막혔습니다. 사용자 계정에 Spotify 측 제한이 걸린 것으로 보이며, 펌웨어로는 해결할 수 없습니다.

**Spotify를 켜 두면 AirPlay가 실패할 수 있습니다.** Spotify 세션이 연결되어 있으면 내부 DRAM 여유가 약 14KB(가장 큰 블록 약 10KB)로 줄어, AirPlay 연결 작업(8KB 스택)을 만들지 못합니다(`rtsp_server: Failed to create client task`). 재생할 수 없는 계정이라면 `/spotify`의 **Spotify Connect 끄기**를 누르고 재부팅하세요. 그러면 검색과 세션이 모두 꺼지고 AirPlay·Bluetooth만 남습니다.

0.3.2 이전 펌웨어는 ②가 실패하면(잘못된 Secret, 인터넷 끊김) 예외가 잡히지 않아 보드가 재부팅을 반복했습니다. 0.3.2부터는 오류를 기록하고 30초 뒤 다시 시도합니다.
