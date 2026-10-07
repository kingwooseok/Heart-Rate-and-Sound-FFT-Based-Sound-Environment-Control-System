# STM32 애플리케이션 재구현

Master와 두 센서 노드의 애플리케이션 코드를 C99로 구성했다. CubeIDE에서 생성하는 MCU 선택, 클럭, 핀, 주변장치 초기화, 인터럽트 설정, startup·linker 파일은 각 보드 프로젝트에 둔다. 이 폴더의 네 C 파일을 해당 프로젝트에 추가하면 생성 코드와 연결할 수 있다.

## 구현 범위

| 파일 | 기능 | 근거와 재구현 범위 |
| --- | --- | --- |
| [`hr_control.c`](hr_control.c) | 심박 입력 스케일링·LED 구간, Master 상태·초 카운터·LCD 문자열, DFPlayer 명령 프레임 | 스케일링과 LED 조건은 [보존 코드](../report-excerpts/heart_rate_slave_functions.c)의 정수식을 유지했다. Master 제어 코드는 [기록된 동작](../../docs/design.md)을 바탕으로 재구현했다. |
| [`hr_sound.c`](hr_sound.c) | 128점 FFT, 피크 bin, UART 프레임, 독립 NLMS 필터 | 8000 Hz·128점·64개 UART 값은 [원본 PC 코드](../../archive/original_fft_viewer.py)의 규격이다. 평균 제거·Hann 창·진폭 보정과 NLMS 연산은 새로 구현했다. |
| [`hr_lcd.c`](hr_lcd.c) | HD44780 호환 16×2 LCD의 GPIO 4비트 쓰기 | LCD 모델과 실제 배선이 남아 있지 않아 표준 병렬 인터페이스를 선택했다. RS·E·D4–D7은 보드 설정으로 받는다. |
| [`hr_stm32f1.c`](hr_stm32f1.c) | STM32F1 HAL 연결, ADC DMA, SPI 양방향 교환, UART, 출력 제어 | 한 애플리케이션에서 `HR_ROLE_MASTER`, `HR_ROLE_SOUND`, `HR_ROLE_HEART` 중 하나를 선택한다. |

`HR`은 ADC 진폭을 변환한 상태 값이다. 맥박 간격에서 BPM이나 HRV를 계산하는 기능은 포함하지 않는다. NLMS는 참조 입력으로 잡음 성분을 추정하고 잔차를 반환하는 독립 알고리즘이다. 스피커 출력과 음향 전달경로 보상은 연결하지 않았으므로 실제 공간의 ANC를 완성한 코드는 아니다.

## 통신과 제어 규칙

모든 SPI 노드는 **8비트, MSB first, Mode 0**을 사용한다. Master는 두 CS 중 하나씩 활성화하여 1바이트를 교환한다. 참고 폴더의 Mode 1 송신 예제는 이 실행 경로에 넣지 않는다.

| 방향 | 1바이트 내용 |
| --- | --- |
| Master → 각 Slave | 누적 초의 하위 8비트. 255 다음에는 0으로 돌아가며 Master의 전체 카운터는 유지된다. |
| 심박 Slave → Master | 기존 수식의 `scaled`, 범위 0–150 |
| 소리 Slave → Master | 가장 큰 비DC 성분의 `peak_bin`, 범위 0–63. 주파수는 `bin × 62.5 Hz`. 무신호는 0. |
| Slave 준비 전·유효하지 않은 입력 | 예약값 `0xFF` |

소리의 **SPI 피크 bin 표현은 이번 재구현에서 정한 형식**이다. 원래 SPI 바이트 배치는 남아 있지 않다. 64개 FFT 진폭은 별도로 UART에 `FFT:값0,값1,...,값63\r\n` 형식으로 보낸다.

Slave는 첫 SCK가 들어오기 전에 `HAL_SPI_TransmitReceive_IT()`를 준비한다. 전송 중인 바이트는 유지하고, 완료 콜백에서 다음 교환을 준비한다. 따라서 Master는 직전 교환 완료 시점에 준비된 센서 스냅샷을 읽는다. 최초 교환에는 `0xFF`가 올 수 있고, 측정 갱신 반영에는 한 번의 폴링 지연이 있다. 두 Slave를 먼저 초기화한 뒤 Master의 폴링을 시작한다. ADC half/full 콜백이 1초 이상 멈추면 센서 노드는 값을 `0xFF`로 바꾸고 심박 LED를 끈다. ADC 오류 콜백을 받으면 재시작할 때까지 유효하지 않은 상태를 유지한다.

기본 집중 조건은 `HR_MASTER_DEFAULT_CONFIG`, 즉 **심박 상태 61–110, 소리 bin 0–63 허용**이다. 심박 구간은 기존 초록 LED 조건을 따랐다. 원래 Master의 소음 임계값이 남아 있지 않아 기본값은 모든 유효 소리 bin을 허용하고 주파수를 표시한다. `hr_board_t.focus`에서 심박·소리 구간을 바꿀 수 있다. 집중 상태에서는 초 카운터를 증가시키고 백색소음 트랙을 반복 재생한다. 범위 이탈이나 SPI 오류·예약값 수신 시 카운터를 초기화하고 정지한다.

Master는 최소 200 ms 간격으로 폴링한다. DFPlayer 부팅 대기 후 `0x08`로 지정 트랙을 반복 재생하고 `0x16`으로 정지한다. 명령은 상태가 바뀌거나 이전 UART 전송이 실패했을 때 보낸다. UART는 9600 baud, 8N1이며 PC 디버그 UART와 별도 인스턴스를 사용한다. 명령과 체크섬은 [DFRobot 공식 프로토콜](https://www.dfrobot.com.cn/images/upload/File/201811051127329r3tdn.pdf)을 따른다. `track`은 준비한 음원 매체의 트랙 번호 1–2999로 지정한다.

## CubeIDE에서 준비할 설정

| 노드 | 필요한 설정 |
| --- | --- |
| 공통 | STM32F1 MCU와 보드에 맞는 시스템 클럭·핀·전원 설정, HAL tick, 생성된 IRQ 핸들러. SPI는 full duplex·8비트·Mode 0·MSB first. |
| Master | SPI Master, software NSS. 두 Slave용 GPIO CS 출력은 초기 HIGH. LCD용 GPIO 출력 6개. DFPlayer용 UART 9600·8N1. |
| 소리 Slave | SPI Slave, hardware NSS input, SPI IRQ 활성화. 소리 센서 ADC 단일 채널·12비트·우측 정렬, 연속 변환 OFF, 타이머 TRGO로 8000 Hz 변환. DMA peripheral→memory, circular, peripheral/memory halfword, memory increment ON, half/full IRQ 활성화. 선택적 디버그 UART 115200·8N1과 UART IRQ. |
| 심박 Slave | SPI Slave, hardware NSS input, SPI IRQ 활성화. ADC 단일 채널·12비트·우측 정렬·연속 변환, DMA circular·halfword·memory increment 및 half/full IRQ. LED 출력 3개. 선택적 디버그 UART 115200·8N1과 UART IRQ. |

소리 DMA 버퍼는 256개 ADC 샘플이다. half/full 콜백마다 연속된 128개 샘플을 전달하므로 8000 Hz에서 한 블록은 16 ms다. FFT는 ISR 밖에서 실행한다. 처리 대기 중 새 블록이 도착하면 `dropped_blocks`를 늘리고 해당 블록을 건너뛴다. 115200 baud UART에는 매 블록을 보내지 않고 약 200 ms 간격으로 최신 스펙트럼을 전송한다.

심박 DMA는 32개 샘플을 사용하고 각 16개 half의 마지막 값을 최신 ADC 값으로 취한다. 1샘플마다 인터럽트를 발생시키던 발췌 코드를 블록 DMA로 바꿨으며 스케일링 식은 유지했다. 원래 배선을 사용할 경우 ADC는 PA0/ADC1_IN0, SPI2는 PB12=NSS·PB13=SCK·PB14=MISO·PB15=MOSI, LED는 PB3=파랑·PB4=초록·PB5=빨강이다. PB3/PB4 사용 시 JTAG를 해제하고 SWD를 유지한다. CubeIDE의 Serial Wire 디버그 설정이나 `__HAL_AFIO_REMAP_SWJ_NOJTAG()`를 사용한다.

LCD는 **HD44780 호환 GPIO 4비트 방식**으로 연결한다. RS·E·D4·D5·D6·D7 순서로 `board.lcd`를 채우고 R/W는 GND에 연결한다. I²C 백팩용 드라이버는 아니다. 전원이 안정된 뒤 초기화를 호출한다. 초기 순서와 고정 대기는 [HD44780U 데이터시트](https://cdn-shop.adafruit.com/datasheets/HD44780.pdf)를 따른다. 짧은 대기는 Cortex-M3의 DWT cycle counter, 긴 대기는 HAL tick을 사용하므로 `SystemCoreClock`이 실제 CPU 클럭과 일치해야 한다. DWT 카운터 값은 초기화하지 않는다. 실제 LCD·CS·UART 핀은 남은 자료에 없어 번호를 지정하지 않았다.

## 생성 코드와 연결

네 `.c` 파일과 해당 `.h` 파일을 CubeIDE의 application source/include 경로에 추가한다. `hr_sound.c`에는 수학 라이브러리 링크(`-lm`)가 필요하다. [`report-excerpts/`](../report-excerpts/)와 [`spi-reference/`](../spi-reference/)는 역사적 코드이므로 함께 컴파일하지 않는다.

아래 핸들 이름은 예시다. 실제 생성된 핸들로 바꾸고 `CS_SOUND`, `CS_HEART`, `LCD_RS`, `LCD_E`, `LCD_D4`–`LCD_D7`은 CubeIDE에서 자신이 선택한 핀에 지정할 **사용자 라벨**이다. 기존 배선의 핀 번호를 뜻하지 않는다.

```c
#include "hr_stm32f1.h"

static hr_stm32_t app;  /* DMA와 작업 버퍼를 포함하므로 static으로 배치한다. */

/* Master 프로젝트: MX_*_Init() 호출 이후의 USER CODE 영역. */
const hr_board_t board = {
    .role = HR_ROLE_MASTER,
    .spi = &hspi1,
    .player_uart = &huart1,
    .chip_select = {
        {CS_SOUND_GPIO_Port, CS_SOUND_Pin},
        {CS_HEART_GPIO_Port, CS_HEART_Pin}
    },
    .lcd = {
        {LCD_RS_GPIO_Port, LCD_RS_Pin},
        {LCD_E_GPIO_Port, LCD_E_Pin},
        {LCD_D4_GPIO_Port, LCD_D4_Pin},
        {LCD_D5_GPIO_Port, LCD_D5_Pin},
        {LCD_D6_GPIO_Port, LCD_D6_Pin},
        {LCD_D7_GPIO_Port, LCD_D7_Pin}
    },
    .focus = HR_MASTER_DEFAULT_CONFIG,
    .track = 1
};

if (!hr_stm32_init(&app, &board)) {
    Error_Handler();
}
while (1) {
    hr_stm32_poll(&app);
}
```

소리 프로젝트에서는 같은 초기화·루프에 다음 `board`를 사용한다. `htim3`는 예시 이름이며 ADC 트리거로 선택한 실제 타이머 핸들로 바꾼다. 타이머 TRGO·ADC 외부 트리거 조합과 8000 Hz는 CubeIDE에서 설정한다. 애플리케이션 초기화가 ADC DMA와 타이머를 시작하므로 생성 코드에서 별도로 중복 시작하지 않는다.

```c
const hr_board_t board = {
    .role = HR_ROLE_SOUND,
    .spi = &hspi2,
    .adc = &hadc1,
    .sample_timer = &htim3,
    .debug_uart = &huart2
};
```

심박 프로젝트는 다음 구성을 사용한다. LED 라벨은 실제 선택한 출력 핀에 부여한다. 디버그 출력이 필요 없으면 두 센서 구성의 `debug_uart`를 생략할 수 있다.

```c
const hr_board_t board = {
    .role = HR_ROLE_HEART,
    .spi = &hspi2,
    .adc = &hadc1,
    .debug_uart = &huart2,
    .led = {
        {LED_BLUE_GPIO_Port, LED_BLUE_Pin},
        {LED_GREEN_GPIO_Port, LED_GREEN_Pin},
        {LED_RED_GPIO_Port, LED_RED_Pin}
    }
};
```

센서 프로젝트의 HAL 콜백에서 애플리케이션 함수를 호출한다. 이미 콜백이 있으면 그 안에 합치고 같은 이름의 함수를 중복 정의하지 않는다. 생성된 DMA·SPI·UART IRQ 핸들러는 해당 `HAL_*_IRQHandler()`를 호출해야 한다.

```c
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc) {
    hr_stm32_adc_half(&app, adc);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc) {
    hr_stm32_adc_full(&app, adc);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc) {
    hr_stm32_adc_error(&app, adc);
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi) {
    hr_stm32_spi_done(&app, spi);
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi) {
    hr_stm32_spi_error(&app, spi);
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart) {
    hr_stm32_uart_done(&app, uart);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {
    hr_stm32_uart_done(&app, uart); /* 디버그 송신 busy 상태를 해제한다. */
}
```

## 보드 없는 검증

MSVC의 경고를 오류로 처리한 호스트 빌드·통합 테스트, Cortex-M3 GCC의 코어 컴파일, STM32CubeF1 V1.8.6의 실제 HAL/CMSIS 헤더를 사용한 어댑터 컴파일을 통과했다. 헤더 검증 대상은 STM32F103xB이며 원래 보드의 정확한 모델을 지정한 것은 아니다. C에서 생성한 1000 Hz FFT 프레임을 기존 Python 뷰어 파서로 읽어 64개 bin과 피크 위치도 검사했다.

저장소 루트에서 호스트 C 컴파일러와 Python 3으로 실행한다. Windows의 `cl`은 Visual Studio Developer Command Prompt에서 사용한다.

```bash
python firmware/reconstructed/tests/run_tests.py --cc cl
# GCC를 사용할 때
python firmware/reconstructed/tests/run_tests.py --cc gcc
```

Cortex-M3 교차 컴파일과 설치된 STM32CubeF1 헤더 확인은 로컬 도구 경로를 추가한다.

```bash
python firmware/reconstructed/tests/run_tests.py --cc gcc \
  --arm-gcc /path/to/arm-none-eabi-gcc \
  --cube-root /path/to/STM32Cube_FW_F1
```

테스트는 스케일링 경계, 집중 전환·카운터, DFPlayer 체크섬, FFT의 독립 DFT 비교, UART 형식, NLMS 수렴, LCD 초기 순서·전송 타이밍을 검사한다. HAL 연결은 호스트 모델로 인터럽트·DMA·SPI 흐름을 검증한다. 교차 컴파일은 실제 STM32 HAL/CMSIS 헤더로 application source를 컴파일하며 최종 보드용 바이너리 링크는 CubeIDE 프로젝트에서 수행한다.

실제 보드에서의 센서 입력·통신 파형·LCD·음원 재생과 처리 시간은 시험하지 않았다. 하드웨어별 설정을 제외한 애플리케이션 코드와 재현 가능한 소프트웨어 검증을 제공한다.
