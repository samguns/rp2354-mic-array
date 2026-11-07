#ifndef MIC_ARRAY_BOARD_PINS_H
#define MIC_ARRAY_BOARD_PINS_H
/* RP2354A board, U5 RP2354A QFN-60. GPIO numbers are not package pin numbers. */
#define MIC_CLOCK_PIN 6u       /* SCLK: U5 pin 9 */
#define MIC_DATA1_PIN 7u       /* DATA1 / U1: U5 pin 10 */
#define MIC_DATA2_PIN 8u       /* DATA2 / U2: U5 pin 12 */
#define MIC_DATA3_PIN 9u       /* DATA3 / U3: U5 pin 13 */
#define MIC_DATA4_PIN 10u      /* DATA4 / U4: U5 pin 14 */
#define SPEAKER_DATA_PIN 11u   /* DIN / U7 pin 3: U5 pin 15 */
#define SPEAKER_BCLK_PIN 12u   /* BCLK / U7 pin 4: U5 pin 16 */
#define SPEAKER_LRC_PIN 13u    /* LRC / U7 pin 5: U5 pin 17 */
#if MIC_DATA2_PIN != MIC_DATA1_PIN+1 || MIC_DATA3_PIN != MIC_DATA1_PIN+2 || MIC_DATA4_PIN != MIC_DATA1_PIN+3
#error "PDM parallel input requires four consecutive data GPIOs"
#endif
#if SPEAKER_BCLK_PIN != SPEAKER_DATA_PIN+1 || SPEAKER_LRC_PIN != SPEAKER_BCLK_PIN+1
#error "I2S output and diagnostic probe require consecutive DIN/BCLK/LRC GPIOs"
#endif
#endif
