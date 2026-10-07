# 설계와 구현

## 목적과 역할 분담

환경 소리와 생체 센서 입력을 함께 받아 상태 표시와 음악 재생으로 연결하는 다중 MCU 시스템을 구성했다. 소리·심박 센서의 입력 처리는 각각의 Slave가 맡고, Master는 두 노드의 값을 받아 LCD와 재생 모듈을 제어한다.

| 노드 | 입력 | 처리 | 출력 |
| --- | --- | --- | --- |
| STM32 Master | SPI 센서 데이터 | 상태 판단, 카운트 관리 | LCD, DFPlayer·스피커, Slave로 CNT 반환 |
| STM32 Slave 1 | 아날로그 소리 센서 | ADC·DMA, FFT, 피크 주파수 추출 | SPI 처리 값, UART FFT 데이터 |
| STM32 Slave 2 | 아날로그 심박 센서 | ADC·DMA, 구간별 스케일링 | SPI 상태 값, LED, UART 디버그 |

회로는 STM32 보드 3대, 소리 센서, 심박 센서, LED 3개, 부저, LCD, DFPlayer와 스피커로 구성된다. 심박 센서 노드의 코드는 STM32F1 계열 레지스터를 사용한다.

## SPI 데이터 교환

Master가 카운트 값을 송신하는 동안 Slave가 처리한 센서 값을 반환하는 full-duplex 구성이다. Slave 1은 FFT 관련 데이터, Slave 2는 스케일링 값을 보낸다.

Slave 2 루프는 `SPI_SR_RXNE`를 확인한 뒤 `SPI2_Slave_TransmitReceive((uint8_t)(scaled & 0xFF))`를 호출한다. 송신 인자는 센서 상태 값이며 수신 바이트는 `rx`에 저장한다.

| Slave 2 항목 | 설정 |
| --- | --- |
| 주변장치 | SPI2, Slave, 하드웨어 NSS |
| SPI 모드 | CPOL=0, CPHA=0, Mode 0 |
| NSS / SCK | PB12 / PB13 |
| MISO / MOSI | PB14 / PB15 |
| 전송 값 | `scaled`의 하위 8비트 |

SPI1 Master 레지스터 설정을 다룬 Mode 1 예제는 [`firmware/spi-reference/`](../firmware/spi-reference/)에 수록했다.

## 심박 센서 노드: ADC와 DMA

심박 센서 노드의 수집 경로는 다음과 같다.

1. PA0를 ADC1_IN0 아날로그 입력으로 설정한다.
2. ADC1의 연속 변환 및 DMA 요청을 활성화한다.
3. DMA1 Channel 1이 ADC1 데이터 레지스터에서 `adc_val`로 16비트 값 1개를 순환 전송한다.
4. 전송 완료 ISR이 플래그를 지우고 `ADC_DMA_Update()`를 호출한다.
5. 갱신된 값을 SPI·LED·UART 출력에 사용한다.

| 항목 | 발췌 코드 설정 |
| --- | --- |
| 변환 채널 / 시퀀스 | ADC1_IN0 / 1개 |
| 샘플 시간 | `SMP0=011`, 28.5 ADC cycles |
| DMA 전송 폭 | Peripheral 16bit, Memory 16bit |
| DMA 개수 / 모드 | 1개 / circular |
| DMA 우선순위 | `PL=01`, Medium |
| 완료 처리 | `DMA1_Channel1_IRQHandler()` → `ADC_DMA_Update()` |

DMA는 ADC 결과를 메모리로 옮기는 작업을 수행한다. 이 구현은 완료 인터럽트에서 스케일링을 수행하므로, 스케일링 연산 자체는 CPU가 담당한다.

### 스케일링 식과 LED

원문 변수 `scaled`는 아래 정수식으로 생성된다. 나눗셈은 C의 정수 나눗셈이다.

| ADC 값 `v` | 계산식 |
| --- | --- |
| `v < 2400` | `(v * 50) / 2400` |
| `2400 ≤ v < 2500` | `50 + (v - 2400) / 20` |
| `2500 ≤ v < 2700` | `60 + ((v - 2500) * 40) / 200` |
| `2700 ≤ v ≤ 3000` | `100 + ((v - 2700) * 40) / 300` |
| `v > 3000` | `150` |

| `scaled` | LED 핀 | 표시 |
| --- | --- | --- |
| `≤ 60` | PB3 | 파랑 |
| `61–110` | PB4 | 초록 |
| `> 110` | PB5 | 빨강 |

**심박 센서 상태 값**은 ADC 진폭을 위 식으로 변환한 수치다. UART의 `HR=` 필드로 출력하며, LED와 SPI 전송에 함께 사용한다.

구현: [심박 센서 노드 코드](../firmware/report-excerpts/heart_rate_slave_functions.c).

## 소리 노드와 PC 시각화

소리 노드는 ADC·DMA를 통한 소리 수집, 시간 영역 파형 구성, FFT, 피크 주파수 추출 순서로 처리한다. 제공된 Python 스크립트는 장치가 이미 계산한 FFT 값을 UART에서 받아 그래프로 표시한다.

| PC 뷰어 원본 설정 | 값 |
| --- | --- |
| UART | COM5, 115200 baud |
| 프레임 | `FFT:` + 쉼표로 구분한 정수 64개 |
| FFT 길이 / 샘플링률 설정 | 128 / 8000 Hz |
| 표시 bin | `k=0…63`, 0…3937.5 Hz |
| bin 간격 | 62.5 Hz |
| 원본 갱신 간격 | 5초 |

PC 도구는 `--fs`와 `--fft-size`로 주파수 축을 설정한다. 송신 펌웨어의 샘플링률과 FFT 길이를 지정하면 각 bin을 해당 주파수에 대응시킬 수 있다.

LMS 기반 소음 제어는 입력·역위상 출력·잔차 피드백으로 구성된다. 마이크 입력 `x[n]`과 잔차 `e[n]`의 파형을 비교했다.

## Master의 출력 제어

Master는 다음 상태 흐름을 사용한다.

| 상태 | 카운트 | 재생 | 표시 |
| --- | --- | --- | --- |
| 집중 모드 | 누적 | 백색소음 재생 | 센서 데이터·CNT |
| 비집중 모드 | 초기화 | MP3 정지 | 센서 데이터·CNT |

## 코드 살펴보기

레지스터 초기화·ISR·메인 루프는 [펌웨어 안내](../firmware/README.md)에서 함수별로 확인할 수 있다. UART 스펙트럼 표시 도구의 실행 방법은 [FFT 뷰어](../tools/README.md)에 정리했다.
