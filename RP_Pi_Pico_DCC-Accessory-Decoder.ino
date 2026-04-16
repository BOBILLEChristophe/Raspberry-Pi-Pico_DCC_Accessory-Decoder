/*
 * ============================================================================================
 *  Projet : Pico DCC Accessory Decoder
 *  Cible  : Raspberry Pi Pico / Pico W (RP2040)
 *  Auteur : Christophe BOBILLE
 *  Version 0.1
 * --------------------------------------------------------------------------------------------
 *  Description :
 *    - Décode les commandes DCC accessoires avec NmraDcc
 *    - Pilote plusieurs servomoteurs
 *    - Peut commander des aiguilles, signaux mécaniques, etc.
 *    - Utilise une logique non bloquante
 * ============================================================================================
 */

#include <Arduino.h>
#include <Servo.h>
#include <NmraDcc.h>

// -------------------------------------------------------------------------------------------------
// CONFIGURATION GENERALE
// -------------------------------------------------------------------------------------------------

constexpr uint8_t DCC_PIN = 2;          // Entrée DCC
constexpr uint8_t MAX_ACCESSORIES = 8;  // Nombre d'accessoires gérés

constexpr uint16_t SERVO_MIN_US = 800;
constexpr uint16_t SERVO_MAX_US = 1800;
constexpr uint8_t SERVO_SPEED_MS = 5;  // plus petit = plus rapide

NmraDcc Dcc;

// -------------------------------------------------------------------------------------------------
// ETATS
// -------------------------------------------------------------------------------------------------

enum MotionState : uint8_t {
  WAIT_COMMAND,
  MOVING,
  HOLD
};

// -------------------------------------------------------------------------------------------------
// STRUCTURE ACCESSOIRE
// -------------------------------------------------------------------------------------------------

struct Accessory {
  uint16_t dccAddress;  // Adresse DCC "finale" de l'accessoire
  uint8_t servoPin;
  uint16_t openPosUs;
  uint16_t closedPosUs;

  Servo servo;
  MotionState state = WAIT_COMMAND;

  uint16_t currentPosUs;
  uint16_t targetPosUs;
  bool output = false;  // false=open, true=closed
  uint32_t lastMoveMs = 0;
  bool attached = false;
};

// -------------------------------------------------------------------------------------------------
// TABLE DES ACCESSOIRES
// -------------------------------------------------------------------------------------------------

// uint16_t dccAddressTab[] = { 150, 151, 152, 153, 154, 155, 156, 157 };
// uint8_t servoPinTab[] = { 2, 5, 8, 11, 14, 17, 20, 26 };
// uint8_t redLedPinTab[] = { 3, 6, 9, 12, 15, 18, 21, 27 };
// uint8_t greenLedPinTab[] = { 4, 7, 10, 13, 16, 19, 22, 28 };


Accessory accessories[MAX_ACCESSORIES] = {
  // dccAddress, servoPin, openPosUs, closedPosUs, ...
  { 150, 1, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 151, 3, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 152, 4, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 153, 5, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 154, 8, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 155, 10, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 156, 13, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false },
  { 157, 15, 800, 1800, Servo(), WAIT_COMMAND, 800, 800, false, 0, false }
};

// -------------------------------------------------------------------------------------------------
// OUTILS
// -------------------------------------------------------------------------------------------------

Accessory* findAccessoryByAddress(uint16_t address) {
  for (uint8_t i = 0; i < MAX_ACCESSORIES; ++i) {
    if (accessories[i].dccAddress == address)
      return &accessories[i];
  }
  return nullptr;
}

void applyAccessoryCommand(Accessory& acc, bool output) {
  acc.output = output;
  acc.targetPosUs = output ? acc.closedPosUs : acc.openPosUs;

  if (!acc.attached) {
    acc.servo.attach(acc.servoPin);
    acc.attached = true;
  }

  acc.lastMoveMs = millis();
  acc.state = MOVING;
}

// -------------------------------------------------------------------------------------------------
// CALLBACKS NmraDcc
// -------------------------------------------------------------------------------------------------

// Mode "Output Addressing"
// Une adresse DCC par accessoire
void notifyDccAccTurnoutOutput(uint16_t addr, uint8_t direction, uint8_t outputPower) {
  Accessory* acc = findAccessoryByAddress(addr);
  if (acc == nullptr)
    return;

  // Sur beaucoup de centrales/logiques :
  // direction = 0 / 1 correspond aux deux états de l'accessoire.
  // outputPower peut servir à distinguer une impulsion active.
  //
  // Ici on prend direction comme état principal.
  const bool output = (direction != 0);
  applyAccessoryCommand(*acc, output);
}

// Optionnel : debug de tous les paquets reçus
/*
void notifyDccMsg(DCC_MSG* msg)
{
  Serial.print("DCC: ");
  for (uint8_t i = 0; i < msg->Size; i++)
  {
    Serial.print(msg->Data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}
*/

// -------------------------------------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Pico DCC Accessory Decoder starting...");

  for (uint8_t i = 0; i < MAX_ACCESSORIES; i++) {

    accessories[i].currentPosUs = accessories[i].openPosUs;
    accessories[i].targetPosUs = accessories[i].openPosUs;

    accessories[i].servo.attach(accessories[i].servoPin);
    accessories[i].servo.writeMicroseconds(accessories[i].currentPosUs);
    delay(100);
    accessories[i].servo.detach();
    accessories[i].attached = false;
  }

  pinMode(DCC_PIN, INPUT);

  // FLAGS_DCC_ACCESSORY_DECODER : décodeur d'accessoires
  // FLAGS_OUTPUT_ADDRESS_MODE   : une adresse par sortie/accessoire logique
  Dcc.pin(DCC_PIN, 0);
  Dcc.init(MAN_ID_DIY, 10, FLAGS_DCC_ACCESSORY_DECODER | FLAGS_OUTPUT_ADDRESS_MODE, 0);
}

// -------------------------------------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------------------------------------

void loop() {
  Dcc.process();

  const uint32_t now = millis();

  for (uint8_t i = 0; i < MAX_ACCESSORIES; i++) {
    Accessory& acc = accessories[i];

    switch (acc.state) {
      case WAIT_COMMAND:
        break;

      case MOVING:

        if (now - acc.lastMoveMs >= SERVO_SPEED_MS) {
          acc.lastMoveMs = now;

          if (acc.currentPosUs < acc.targetPosUs) ++acc.currentPosUs;
          if (acc.currentPosUs > acc.targetPosUs) --acc.currentPosUs;

          acc.servo.writeMicroseconds(acc.currentPosUs);

          Serial.println(acc.currentPosUs);

          if (acc.currentPosUs == acc.targetPosUs) {
            acc.state = HOLD;
          }
        }
        break;

      case HOLD:
        if (acc.attached) {
          acc.servo.detach();
          acc.attached = false;
        }
        acc.state = WAIT_COMMAND;
        break;
    }
  }
}
