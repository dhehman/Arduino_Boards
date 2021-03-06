/*
  Copyright (c) 2014 Arduino LLC.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Arduino.h"
#include "wiring_private.h"

#ifdef __cplusplus
extern "C" {
#endif

static int _readResolution = 10;
static int _ADCResolution = 10;
static int _writeResolution = 8;

// Wait for synchronization of registers between the clock domains
static __inline__ void syncADC() __attribute__((always_inline, unused));
static void syncADC() {
  while (ADC->STATUS.bit.SYNCBUSY == 1)
    ;
}

// Wait for synchronization of registers between the clock domains
static __inline__ void syncDAC() __attribute__((always_inline, unused));
static void syncDAC() {
  while (DAC->STATUS.bit.SYNCBUSY == 1)
    ;
}

// Wait for synchronization of registers between the clock domains
static __inline__ void syncTC_8(Tc* TCx) __attribute__((always_inline, unused));
static void syncTC_8(Tc* TCx) {
  while (TCx->COUNT8.STATUS.bit.SYNCBUSY);
}

// Wait for synchronization of registers between the clock domains
static __inline__ void syncTCC(Tcc* TCCx) __attribute__((always_inline, unused));
static void syncTCC(Tcc* TCCx) {
  while (TCCx->SYNCBUSY.reg & TCC_SYNCBUSY_MASK);
}

//void analogReadResolution( int res )
//{
//  _readResolution = res ;
//  if (res > 10)
//  {
//    ADC->CTRLB.bit.RESSEL = ADC_CTRLB_RESSEL_12BIT_Val;
//    _ADCResolution = 12;
//  }
//  else if (res > 8)
//  {
//    ADC->CTRLB.bit.RESSEL = ADC_CTRLB_RESSEL_10BIT_Val;
//    _ADCResolution = 10;
//  }
//  else
//  {
//    ADC->CTRLB.bit.RESSEL = ADC_CTRLB_RESSEL_8BIT_Val;
//    _ADCResolution = 8;
//  }
//  syncADC();
//}

//void analogWriteResolution( int res )
//{
//  _writeResolution = res ;
//}

//static inline uint32_t mapResolution( uint32_t value, uint32_t from, uint32_t to )
//{
//  if ( from == to )
//  {
//    return value ;
//  }
//
//  if ( from > to )
//  {
//    return value >> (from-to) ;
//  }
//  else
//  {
//    return value << (to-from) ;
//  }
//}

/*
 * Internal Reference is at 1.0v
 * External Reference should be between 1v and VDDANA-0.6v=2.7v
 *
 * Warning : On Arduino Zero board the input/output voltage for SAMD21G18 is 3.3 volts maximum
 */
void analogReference(eAnalogReference mode)
{
  syncADC();
  switch (mode)
  {
    case AR_INTERNAL:
    case AR_INTERNAL2V23:
      ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_1X_Val;      // Gain Factor Selection
      ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_INTVCC0_Val; // 1/1.48 VDDANA = 1/1.48* 3V3 = 2.2297
      break;

    case AR_EXTERNAL:
      ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_1X_Val;      // Gain Factor Selection
      ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_AREFA_Val;
      break;

    case AR_INTERNAL1V0:
      ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_1X_Val;      // Gain Factor Selection
      ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_INT1V_Val;   // 1.0V voltage reference
      break;

    case AR_INTERNAL1V65:
      ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_1X_Val;      // Gain Factor Selection
      ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_INTVCC1_Val; // 1/2 VDDANA = 0.5* 3V3 = 1.65V
      break;

    case AR_DEFAULT:
    default:
      ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_DIV2_Val;
      ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_INTVCC1_Val; // 1/2 VDDANA = 0.5* 3V3 = 1.65V
      break;
  }
  analogRead(0); // Do a dummy conversion, necessary after reference change
}

uint32_t analogRead(uint32_t pin)
{
  uint32_t valueRead = 0;
  uint32_t channel;

  if ((pin >= 0x18)&&(pin <= 0x1C)) // Internal ADC sources
  {
	channel = pin; // Grab source directly from parameter
  }
  else // External ADC source
  {
    if ((pin > PINS_COUNT)||(g_APinDescription[pin].ulPinType == PIO_NOT_A_PIN)||(g_APinDescription[pin].ulADCChannelNumber == No_ADC_Channel))
    {
      return 0;
    }

    // Mux pin to ADC
    pinPeripheral(pin, PIO_ANALOG);

/*
  if (pin == A0) // Disable DAC, if analogWrite(A0,dval) used previously the DAC is enabled
  {
    syncDAC();
    DAC->CTRLA.bit.ENABLE = 0x00; // Disable DAC
    //DAC->CTRLB.bit.EOEN = 0x00; // The DAC output is turned off.
    syncDAC();
  }
*/
	channel = g_APinDescription[pin].ulADCChannelNumber; // Input selection
  }

  // Postpone IRQ past the ADC read to preserve the conversion (ADC hardware is depowered in sleep)
  __disable_irq();

  syncADC();
  ADC->INPUTCTRL.bit.MUXPOS = channel;
  //ADC->INPUTCTRL.bit.MUXNEG = ADC_INPUTCTRL_MUXNEG_GND_Val; // Ground reference was set in init();

  // Control A
  /*
   * Bit 1 ENABLE: Enable
   *   0: The ADC is disabled.
   *   1: The ADC is enabled.
   * Due to synchronization, there is a delay from writing CTRLA.ENABLE until the peripheral is enabled/disabled. The
   * value written to CTRL.ENABLE will read back immediately and the Synchronization Busy bit in the Status register
   * (STATUS.SYNCBUSY) will be set. STATUS.SYNCBUSY will be cleared when the operation is complete.
   *
   * Before enabling the ADC, the asynchronous clock source must be selected and enabled, and the ADC reference must be
   * configured. The first conversion after the reference is changed must not be used.
   */
  syncADC();
  ADC->CTRLA.bit.ENABLE = 1;             // Enable ADC

/*
  // Start conversion
  syncADC();
  ADC->SWTRIG.bit.START = 1;

  syncADC();
  // Clear the Data Ready flag
//  ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY;
  ADC->INTFLAG.bit.RESRDY = 1;

  // Start conversion again, since The first conversion after the reference is changed must not be used.
*/

  syncADC();
  ADC->SWTRIG.bit.START = 1;

  // Store the value
  while (ADC->INTFLAG.bit.RESRDY == 0);   // Waiting for conversion to complete
  valueRead = ADC->RESULT.reg;

  syncADC();
  ADC->CTRLA.bit.ENABLE = 0x00;             // Disable ADC
  syncADC(); // needed?

  __enable_irq();

//  return mapResolution(valueRead, _ADCResolution, _readResolution);
  return valueRead;
}


// Right now, PWM output only works on the pins with
// hardware support.  These are defined in the appropriate
// pins_*.c file.  For the rest of the pins, we default
// to digital output.
void analogWrite(uint32_t pin, uint32_t value)
{
  PinDescription pinDesc = g_APinDescription[pin];
  uint32_t attr = pinDesc.ulPinAttribute;

// DAC removed in LilyMini for space / simplicity (use PWM)

//  value = mapResolution(value, _writeResolution, 8);

  if ((attr & PIN_ATTR_PWM) == PIN_ATTR_PWM)
  {
    if (attr & PIN_ATTR_TIMER) {
      #if !(ARDUINO_SAMD_VARIANT_COMPLIANCE >= 10603)
      // Compatibility for cores based on SAMD core <=1.6.2
      if (g_APinDescription[pin].ulPinType == PIO_TIMER_ALT) {
        pinPeripheral(pin, PIO_TIMER_ALT);
      } else
      #endif
      {
        pinPeripheral(pin, PIO_TIMER);
      }
    } else {
      // We suppose that attr has PIN_ATTR_TIMER_ALT bit set...
      pinPeripheral(pin, PIO_TIMER_ALT);
    }

    Tc*  TCx  = 0 ;
    Tcc* TCCx = 0 ;
    uint8_t Channelx = GetTCChannelNumber( g_APinDescription[pin].ulPWMChannel ) ;
    if ( GetTCNumber( g_APinDescription[pin].ulPWMChannel ) >= TCC_INST_NUM )
    {
      TCx = (Tc*) GetTC( g_APinDescription[pin].ulPWMChannel ) ;
    }
    else
    {
      TCCx = (Tcc*) GetTC( g_APinDescription[pin].ulPWMChannel ) ;
    }

    // Enable clocks according to TCCx instance to use
    switch ( GetTCNumber( g_APinDescription[pin].ulPWMChannel ) )
    {
      case 0: // TCC0
        // Enable GCLK for TCC0 (timer counter input clock)
        GCLK->CLKCTRL.reg = (uint16_t) (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID( GCM_TCC0 )) ;
      break ;

      case 1: // TC1
      case 2: // TC2
        // Enable GCLK for TC1 and TC2 (timer counter input clock)
        GCLK->CLKCTRL.reg = (uint16_t) (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID( GCM_TC1_TC2 )) ;
      break ;
    }

    while (GCLK->STATUS.bit.SYNCBUSY == 1);

    // Set PORT
    if ( TCx )
    {
      // -- Configure TC

      // Disable TCx
      TCx->COUNT8.CTRLA.reg &= ~TC_CTRLA_ENABLE;
      syncTC_8(TCx);
      // Set Timer counter Mode to 8 bits
      TCx->COUNT8.CTRLA.reg |= TC_CTRLA_MODE_COUNT8;
      // Set TCx as normal PWM
      TCx->COUNT8.CTRLA.reg |= TC_CTRLA_WAVEGEN_NPWM;
      // Set TCx in waveform mode Normal PWM
      TCx->COUNT8.CC[Channelx].reg = (uint8_t) value;
      syncTC_8(TCx);
      // Set PER to maximum counter value (resolution : 0xFF)
      TCx->COUNT8.PER.reg = 0xFF;
      syncTC_8(TCx);
      // Enable TCx
      TCx->COUNT8.CTRLA.reg |= TC_CTRLA_ENABLE;
      syncTC_8(TCx);
    }
    else
    {
      // -- Configure TCC
      // Disable TCCx
      TCCx->CTRLA.reg &= ~TCC_CTRLA_ENABLE;
      syncTCC(TCCx);
      // Set TCx as normal PWM
      TCCx->WAVE.reg |= TCC_WAVE_WAVEGEN_NPWM;
      syncTCC(TCCx);
      // Set TCx in waveform mode Normal PWM
      TCCx->CC[Channelx].reg = (uint32_t)value;
      syncTCC(TCCx);
      // Set PER to maximum counter value (resolution : 0xFF)
      TCCx->PER.reg = 0xFF;
      syncTCC(TCCx);
      // Enable TCCx
      TCCx->CTRLA.reg |= TCC_CTRLA_ENABLE ;
      syncTCC(TCCx);
    }
    return;
  }

  // -- Defaults to digital write
  pinMode(pin, OUTPUT);

  if (value < 128)
  {
    digitalWrite(pin, LOW);
  }
  else
  {
    digitalWrite(pin, HIGH);
  }
}

#ifdef __cplusplus
}
#endif
