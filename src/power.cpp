// Basic config
#include "globals.h"
#include "power.h"

// Local logging tag
static const char TAG[] = __FILE__;

#ifdef HAS_PMU

AXP20X_Class pmu;

void AXP192_init(void) {

  // block i2c bus access
  if (I2C_MUTEX_LOCK()) {

    if (pmu.begin(Wire, AXP192_PRIMARY_ADDRESS))
      ESP_LOGI(TAG, "AXP192 PMU initialization failed");
    else {

      pmu.setPowerOutPut(AXP192_LDO2, AXP202_ON);
      pmu.setPowerOutPut(AXP192_LDO3, AXP202_ON);
      pmu.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
      pmu.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
      pmu.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
      pmu.setDCDC1Voltage(3300);
      pmu.setChgLEDMode(AXP20X_LED_LOW_LEVEL);
      pmu.adc1Enable(AXP202_BATT_CUR_ADC1, 1);

      /*

      // I2C access of AXP202X library currently is not mutexable
      // so we need to disable AXP interrupts
      
      #ifdef PMU_INT
          pinMode(PMU_INT, INPUT_PULLUP);
          attachInterrupt(digitalPinToInterrupt(PMU_INT),
                          [] {
                            ESP_LOGI(TAG, "Power source changed");
                            // put your code here
                          },
                          FALLING);
          pmu.enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ |
                            AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ,
                        1);
          pmu.clearIRQ();
      #endif // PMU_INT
      */

      ESP_LOGI(TAG, "AXP192 PMU initialized.");
      if (pmu.isBatteryConnect())
        if (pmu.isChargeing())
          ESP_LOGI(TAG, "Running on battery, charging.");
        else
          ESP_LOGI(TAG, "Running on battery, not charging.");
      else if (pmu.isVBUSPlug())
        ESP_LOGI(TAG, "Running on USB power.");
    }
    I2C_MUTEX_UNLOCK(); // release i2c bus access
  } else
    ESP_LOGE(TAG, "I2c bus busy - PMU initialization error");
}
#endif // HAS_PMU

#ifdef BAT_MEASURE_ADC
esp_adc_cal_characteristics_t *adc_characs =
    (esp_adc_cal_characteristics_t *)calloc(
        1, sizeof(esp_adc_cal_characteristics_t));

#ifndef BAT_MEASURE_ADC_UNIT // ADC1
static const adc1_channel_t adc_channel = BAT_MEASURE_ADC;
#else // ADC2
static const adc2_channel_t adc_channel = BAT_MEASURE_ADC;
#endif
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

#endif // BAT_MEASURE_ADC

void calibrate_voltage(void) {
#ifdef BAT_MEASURE_ADC
// configure ADC
#ifndef BAT_MEASURE_ADC_UNIT // ADC1
  ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(adc_channel, atten));
#else // ADC2
  // ESP_ERROR_CHECK(adc2_config_width(ADC_WIDTH_BIT_12));
  ESP_ERROR_CHECK(adc2_config_channel_atten(adc_channel, atten));
#endif
  // calibrate ADC
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
      unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_characs);
  // show ADC characterization base
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    ESP_LOGI(TAG,
             "ADC characterization based on Two Point values stored in eFuse");
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    ESP_LOGI(TAG,
             "ADC characterization based on reference voltage stored in eFuse");
  } else {
    ESP_LOGI(TAG, "ADC characterization based on default reference voltage");
  }
#endif
}

uint8_t getBattLevel() {
  /*
  return values:
  MCMD_DEVS_EXT_POWER   = 0x00, // external power supply
  MCMD_DEVS_BATT_MIN    = 0x01, // min battery value
  MCMD_DEVS_BATT_MAX    = 0xFE, // max battery value
  MCMD_DEVS_BATT_NOINFO = 0xFF, // unknown battery level
  */
#if (defined HAS_PMU || defined BAT_MEASURE_ADC)
  uint16_t voltage = read_voltage();

  switch (voltage) {
  case 0:
    return MCMD_DEVS_BATT_NOINFO;
  case 0xffff:
    return MCMD_DEVS_EXT_POWER;
  default:
    return (voltage > OTA_MIN_BATT ? MCMD_DEVS_BATT_MAX : MCMD_DEVS_BATT_MIN);
  }
#else // we don't have any info on battery level
  return MCMD_DEVS_BATT_NOINFO;
#endif
} // getBattLevel()

// u1_t os_getBattLevel(void) { return getBattLevel(); };

uint16_t read_voltage() {

  uint16_t voltage = 0;

#ifdef HAS_PMU
  voltage = pmu.isVBUSPlug() ? 0xffff : pmu.getBattVoltage();
#else

#ifdef BAT_MEASURE_ADC
  // multisample ADC
  uint32_t adc_reading = 0;
  int adc_buf = 0;
  for (int i = 0; i < NO_OF_SAMPLES; i++) {
#ifndef BAT_MEASURE_ADC_UNIT // ADC1
    adc_reading += adc1_get_raw(adc_channel);
#else                        // ADC2
    ESP_ERROR_CHECK(adc2_get_raw(adc_channel, ADC_WIDTH_BIT_12, &adc_buf));
    adc_reading += adc_buf;
#endif
  }
  adc_reading /= NO_OF_SAMPLES;
  // Convert ADC reading to voltage in mV
  voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_characs);
#endif // BAT_MEASURE_ADC

#ifdef BAT_VOLTAGE_DIVIDER
  voltage *= BAT_VOLTAGE_DIVIDER;
#endif // BAT_VOLTAGE_DIVIDER

#endif // HAS_PMU

  return voltage;
}