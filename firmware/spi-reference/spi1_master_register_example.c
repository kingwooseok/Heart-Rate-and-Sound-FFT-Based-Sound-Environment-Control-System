/*
 * Historical SPI reference example from spi통신관련.docx.
 * Original paragraph code preserved; this is not the final system firmware.
 * Required STM32 project files and initialization are omitted by the source.
 * See ../README.md.
 */

/* --- SPI 핀/클럭 초기화 함수 (main 함수 호출 전) --- */
void SPI1_Init_Register(void)
{
  /* 1. 클럭 활성화 */
  // GPIOA 클럭 활성화 (PA4, PA5, PA6, PA7 핀 사용)
  RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
  // SPI1 클럭 활성화 (SPI1은 APB2 버스 소속)
  RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
  // AFIO(대체 기능) 클럭 활성화 (필수)
  RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

  /* 2. GPIO 핀 설정 (PA5, PA6, PA7은 SPI 기능 / PA4는 CS용 일반 출력) */
  // PA5(SCK), PA7(MOSI) 핀을 '대체 기능 출력 (AF-PP) 50MHz'로 설정
  GPIOA->CRL |= (GPIO_CRL_MODE5 | GPIO_CRL_CNF5_1 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7_1);
  GPIOA->CRL &= ~(GPIO_CRL_CNF5_0 | GPIO_CRL_CNF7_0);
  
  // PA6(MISO) 핀을 '플로팅 입력'으로 설정
  GPIOA->CRL |= GPIO_CRL_CNF6_0;
  GPIOA->CRL &= ~(GPIO_CRL_CNF6_1 | GPIO_CRL_MODE6);
  
  // PA4(CS/SS) 핀을 '일반 출력 (GP-PP) 50MHz'로 설정
  GPIOA->CRL |= GPIO_CRL_MODE4;
  GPIOA->CRL &= ~GPIO_CRL_CNF4;
  
  // CS핀(PA4)을 High로 초기화 (비활성 상태)
  GPIOA->BSRR = GPIO_BSRR_BS4;

  /* 3. SPI_CR1 레지스터 설정 */
  // CPHA=1 (두 번째 엣지), CPOL=0 (Idle 시 Low) 설정
  SPI1->CR1 = (SPI_CR1_CPHA);
  // 통신 속도(Baud Rate) 설정: fPCLK / 16 (011)
  SPI1->CR1 |= SPI_CR1_BR_1 | SPI_CR1_BR_0;
  // SSM=1 (소프트웨어 슬레이브 관리), SSI=1 (내부 슬레이브 선택)
  SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;
  // MSTR=1 (마스터 모드) 설정
  SPI1->CR1 |= SPI_CR1_MSTR;

  /* 4. SPI 활성화 */
  // SPE 비트(Bit 6)를 1로 설정하여 SPI1 모듈을 켬
  SPI1->CR1 |= SPI_CR1_SPE;
}

/* --- SPI 1바이트 송신 함수 (main 함수 호출 전) --- */
void SPI1_Transmit_Register(uint8_t data)
{
  // SPI_SR 레지스터의 TXE 플래그(Bit 1)가 1이 될 때까지 (송신 버퍼가 빌 때까지) 대기
  while (!(SPI1->SR & SPI_SR_TXE));
  
  // 송신할 1바이트 데이터를 데이터 레지스터(SPI_DR)에 씀
  SPI1->DR = data;
  
  // SPI_SR 레지스터의 BSY 플래그(Bit 7)가 0이 될 때까지 (통신이 끝날 때까지) 대기
  while (SPI1->SR & SPI_SR_BSY);
}

/* --- main() 함수 내부에 배치할 SPI 송신 예시 코드 --- */
int main(void)
{
  /* ... (HAL_Init(), SystemClock_Config() 등) ... */
  
  // SPI1 및 관련 GPIO 핀들을 레지스터로 초기화
  SPI1_Init_Register();

  // SPI로 전송할 4바이트 데이터를 담은 버퍼를 선언 및 초기화
  uint8_t txDataBuffer[4] = {0xFF, 0x0F, 0xF0, 0x00};
  
  /* Infinite loop */
  while (1)
  {
    /* USER CODE BEGIN 3 */

    // 1. (NSS_SOFT 모드) CS 핀(PA4)을 LOW로 내려 슬레이브 장치를 선택함 (BSRR의 BR4 비트 사용)
    GPIOA->BSRR = GPIO_BSRR_BR4;
    
    // 2. 버퍼의 모든 데이터를 1바이트씩 전송
    for (int i = 0; i < 4; i++)
    {
      // 1바이트 송신 함수 호출
      SPI1_Transmit_Register(txDataBuffer[i]);
    }
    
    // 3. (NSS_SOFT 모드) CS 핀(PA4)을 HIGH로 올려 슬레이브 장치 선택을 해제함 (BSRR의 BS4 비트 사용)
    GPIOA->BSRR = GPIO_BSRR_BS4;
    
    // 다음 통신까지 1초 대기
    HAL_Delay(1000);
    
    /* USER CODE END 3 */
  }
}
