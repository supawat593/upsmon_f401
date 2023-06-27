#include "mbed.h"

#define BLINKING_RATE 500ms

DigitalOut myled(LED1);
// main() runs in its own thread in the OS
int main() {
  while (true) {
    myled = !myled;
    ThisThread::sleep_for(BLINKING_RATE);
  }
}
