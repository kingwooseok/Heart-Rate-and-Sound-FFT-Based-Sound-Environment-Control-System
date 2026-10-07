# FFT 시각화 도구

MCU가 UART로 출력한 FFT bin을 PC의 주파수 그래프로 표시한다. 실시간 UART 수신과 저장된 로그 재생을 지원한다.

## 실행

Python 3.10 이상에서 저장소 루트 기준으로 실행한다.

```bash
python -m pip install -r requirements.txt
python tools/fft_viewer.py --port COM5
```

Linux에서는 `--port /dev/ttyUSB0`처럼 실제 장치 이름을 지정한다. 창을 닫거나 Ctrl+C를 누르면 포트를 반환하고 종료한다.

| 옵션 | 기본값 | 의미 |
|---|---:|---|
| `--port` | 직접 지정 | UART 포트 |
| `--baud` | `115200` | UART 전송 속도 |
| `--fs` | `8000` | 주파수 축 계산에 사용하는 샘플링 주파수(Hz) |
| `--fft-size` | `128` | FFT 길이; 수신 bin 수는 절반 |
| `--update-interval` | `0.1` | 그래프 갱신·파일 재생 간격(초) |
| `--input-file` | 직접 지정 | UART 텍스트 로그 재생 (`--port` 대신 사용) |

`--fs`와 `--fft-size`를 펌웨어 설정에 맞추면 각 bin의 주파수 축이 계산된다. 기본값 기준으로 주파수 간격은 62.5 Hz이고, 64개 bin의 중심 주파수는 0~3937.5 Hz이다. 세로축은 펌웨어가 출력한 FFT magnitude이며 값에 맞춰 범위를 조정한다.

## UART 형식과 로그 재생

기본 설정에서는 각 줄에 `FFT:` 뒤로 쉼표로 구분한 정수 64개(FFT 길이의 절반)를 담는다. 줄 끝 쉼표와 `FFT:` 앞의 로그 문자열을 허용한다. 일반 로그는 건너뛰고, 잘못된 FFT 프레임 수는 종료할 때 출력한다. 타임아웃으로 나뉘어 수신된 데이터는 줄바꿈까지 이어 붙인다.

UTF-8로 저장한 UART 로그는 지정한 간격으로 순차 재생하며 마지막 그래프를 유지한다.

```bash
python tools/fft_viewer.py --input-file capture.txt --update-interval 0.1
```

## 정리 내용

포트·FFT 설정을 CLI 인자로 옮기고, 갱신 간격을 기본 0.1초로 설정했다. 원본의 5초 간격은 `--update-interval 5`로 사용할 수 있다. UART 타임아웃, 분할 수신 버퍼, 오류 프레임 처리, 안전한 종료를 추가하고 불필요한 NumPy 직접 의존성과 단독 숫자 표현을 제거했다. [원본 코드](../archive/original_fft_viewer.py)도 함께 보존했다.

Python 구문 검사, 프레임 파서 정상·오류 입력, 분할 수신·버퍼 상한 처리, `--help`, CLI 인자 검증을 통과했다.
