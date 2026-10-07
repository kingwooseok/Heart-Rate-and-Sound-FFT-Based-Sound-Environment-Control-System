# 펌웨어 구현

빌드에 연결할 응용 코드는 [`reconstructed/`](reconstructed/README.md)에 있습니다. 심박 스케일링·LED, 128점 FFT·UART 프레임, Master 타이머·집중 상태, SPI 교환, LCD와 DFPlayer 제어를 포함합니다. CubeIDE 생성 코드에 연결하는 설정·콜백 예제와 테스트 명령도 함께 제공합니다.

아래는 기존 발췌 코드의 기록입니다. 새 프로젝트에서는 재구성 소스 4개를 사용하며, 발췌 코드와 중복해서 빌드하지 않습니다.

심박 센서 노드는 ADC1으로 아날로그 입력을 연속 변환하고, DMA1으로 최신 값을 메모리에 저장한다. 전송 완료 인터럽트에서 입력을 스케일링한 뒤 SPI 교환에 전달하며, 메인 루프에서는 LED 상태와 UART 디버그 출력을 갱신한다. UART의 `ADC=`는 원시 입력값, `HR=`는 `scaled` 값이다.

## 코드 구성

| 파일 | 구현 내용 |
| --- | --- |
| [heart_rate_slave_functions.c](report-excerpts/heart_rate_slave_functions.c) | SPI2·GPIO·ADC1·DMA1 초기화와 입력 스케일링 |
| [dma_irq_handler.c](report-excerpts/dma_irq_handler.c) | DMA1 Channel 1 전송 완료 플래그 처리 및 `ADC_DMA_Update()` 호출 |
| [main_loop_snippet.c](report-excerpts/main_loop_snippet.c) | SPI 교환 호출, LED 제어, UART 출력의 `while (1)` 블록 |
| [spi1_master_register_example.c](spi-reference/spi1_master_register_example.c) | SPI1 Master 초기화와 4바이트 송신 예제 |

## 주요 설정

| 기능 | 설정 |
| --- | --- |
| ADC1 | PA0 / Channel 0, 연속 변환, 샘플링 시간 28.5 ADC cycles (`SMP0=011`) |
| DMA1 Channel 1 | Peripheral → Memory, 16비트, 전송 개수 1, Circular mode, Medium 우선순위, 전송 완료 인터럽트 |
| SPI2 Slave | Mode 0, 하드웨어 NSS, PB12–PB15 사용 |
| SPI1 Master 참고 예제 | Mode 1, PA4 소프트웨어 CS, 전송 클럭 `PCLK / 16` |
| LED | `scaled ≤ 60`: PB3, `61–110`: PB4, `> 110`: PB5 |
| UART | 원시 ADC 값과 스케일 값 출력, 루프 말미 200 ms 대기 |

## 프로젝트 통합

STM32 프로젝트에 장치 헤더·CMSIS/HAL·startup·linker 설정을 준비하고, 시스템·ADC 클럭과 UART·NVIC 초기화를 연결한다. `adc_val`, `scaled`, `buf`, `huart2` 선언 및 `SPI2_Slave_TransmitReceive()` 구현을 연결한 뒤 메인 루프 발췌본을 `main()` 내부에 배치한다.

PB3/PB4를 LED GPIO로 사용하려면 `AFIO_MAPR.SWJ_CFG=010`으로 JTAG를 해제하고 SWD를 유지한다. 이때 PB3의 비동기 trace 기능은 사용하지 않는다. SPI 노드를 연결할 때는 양쪽의 CPOL·CPHA를 동일하게 설정한다.

`report-excerpts/`와 `spi-reference/`는 기록용 발췌본이다. 재구성 응용 코드의 빌드·통합 방법은 [`reconstructed/README.md`](reconstructed/README.md)를 따른다.
