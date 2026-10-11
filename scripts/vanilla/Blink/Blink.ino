// Blink.ino — the Arduino IDE's own Blink, unchanged in substance.
//
// Why this file exists: installing a board definition must not change what an
// ordinary sketch does. Every example in examples/ includes UnoR4TinyUSB.h,
// so the example matrix always links the weak callbacks the library provides
// and can never see a default TinyUSB configuration that demands callbacks the
// SKETCH has to write. This sketch includes nothing, so it links against the
// core and the shipped variant config alone -- which is exactly the case that
// broke: CFG_TUD_USBTMC in the default config made plain Blink fail to link
// with 17 undefined tud_usbtmc_* references under both the board package and
// the patched stock core.

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
}
