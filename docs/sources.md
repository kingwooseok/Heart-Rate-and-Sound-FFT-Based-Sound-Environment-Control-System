# 프로젝트 자료

## 프로젝트 파일

| 자료 | 내용 |
| --- | --- |
| [SPI 참고 코드](../firmware/spi-reference/spi1_master_register_example.c) | SPI1 Master 초기화와 4바이트 전송 예제 |
| [시연 녹화](../media/demo.mp4) | 제공된 프로젝트 녹화 파일 |
| [원본 PC 코드](../archive/original_fft_viewer.py) | FFT 시각화 스크립트 |

## 구현별 찾아보기

| 주제 | 정리 문서 |
| --- | --- |
| 전체 회로·노드별 역할 | [설계·구현](design.md) |
| ADC·DMA·SPI2·LED | [펌웨어 코드](../firmware/README.md) |
| LMS 처리 구조와 파형 | [시연·계측](results.md) |
| Master 출력 제어 | [설계·구현](design.md#master의-출력-제어) |
| SPI 양방향 데이터 흐름 | [설계·구현](design.md#spi-데이터-교환) |
| DMA 전후 cycle | [시연·계측](results.md#dma-적용-전후-기록) |
| 하드웨어와 FFT 화면 | [시연·계측](results.md#시연-구성) |
| UART 프레임·그래프 설정 | [PC 도구](../tools/README.md) |

## 레지스터 참고 자료

STMicroelectronics [RM0008](https://www.keil.com/dd/docs/datashts/st/stm32f10xxx.pdf), Rev. 9.

- Table 33: 디버그 핀 매핑
- §10.4.3: DMA 채널 설정
- §11.12.5: ADC 샘플 시간
- §29.4.4: SWD 사용 시 GPIO 해제
