#include <TimerOne.h> // https://code.google.com/archive/p/arduino-timerone/downloads
#include <hidboot.h> // https://github.com/felis/USB_Host_Shield_2.0

#define NUM_DIFFS 128
#define ADB_PIN 2
#define WAITING_FOR_ATTENTION 0
#define WAITING_FOR_SYNC 1
#define READING_COMMAND_BITS 2
#define READING_COMMAND_ARGS 4
#define TALK 3
#define LISTEN 2

// Tweak these to adjust sent pulses so that they
// are measured equal to 35usec/65usec on the logic analyzer!
//#define SHORT_PULSE_DURATION 35
//#define LONG_PULSE_DURATION 65
#define SHORT_PULSE_DURATION 30
#define LONG_PULSE_DURATION 55
#define CLICK_DEBOUNCE_COOLDOWN_MS 50

// Tweak to change mouse speed & max speed
#define MAX_SPEED 50
#define SPEED_DIVISOR 2

volatile unsigned int dx = 0, dy = 0, buttonState = 1;
volatile int haveDataToSend = 0;

class MouseRptParser : public MouseReportParser {
protected:
    void OnMouseMove(MOUSEINFO *mi) {
        dx = 0;
        dy = 0;
        
        if (mi->dX > 0) {
            dx = (mi->dX > MAX_SPEED) ? MAX_SPEED : (mi->dX / SPEED_DIVISOR);
        }
        if (mi->dX < 0) {
            dx = (mi->dX < -MAX_SPEED) ? (0x7F - MAX_SPEED) : (0x7F + (mi->dX / SPEED_DIVISOR));
        }
        
        if (mi->dY > 0) {
            dy = (mi->dY > MAX_SPEED) ? MAX_SPEED : (mi->dY / SPEED_DIVISOR);
        }
        if (mi->dY < 0) {
            dy = (mi->dY < -MAX_SPEED) ? (0x7F - MAX_SPEED) : (0x7F + (mi->dY / SPEED_DIVISOR));
        }
        
        if (dy != 0 || dx != 0) {
            haveDataToSend = 1;
        }
    };
    void OnLeftButtonDown(MOUSEINFO *mi) {
        buttonState = 0;
        haveDataToSend = 1;
    };
    void OnLeftButtonUp(MOUSEINFO *mi) {
        buttonState = 1;
        haveDataToSend = 1;
    };
};
USB Usb;
HIDBoot<USB_HID_PROTOCOL_MOUSE> HidMouse(&Usb);
MouseRptParser MousePrs;

volatile unsigned long diff, startTime, endTime, args;
volatile unsigned long diffs[NUM_DIFFS];
volatile unsigned int count = 0, state = WAITING_FOR_ATTENTION;
volatile unsigned char command;
volatile unsigned char myAddress = 3;
volatile int srqEnabled = 1;
volatile int handlerId = 2;

void setup() {
    pinMode(ADB_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ADB_PIN), adbStateChanged, CHANGE);
    Timer1.initialize(10000);
    Timer1.stop();
    Timer1.restart();
    Timer1.detachInterrupt();
    Usb.Init();
    delay(200);
    HidMouse.SetReportParser(0, &MousePrs);
}

void loop() {
    Usb.Task();
}

void lowPulse(unsigned int duration) {
    pinMode(ADB_PIN, OUTPUT);
    digitalWrite(ADB_PIN, LOW);
    delayMicroseconds(duration);
    digitalWrite(ADB_PIN, HIGH);
    pinMode(ADB_PIN, INPUT_PULLUP);
}

void send(boolean b) {
    pinMode(ADB_PIN, OUTPUT);
    digitalWrite(ADB_PIN, LOW);
    delayMicroseconds(b ? SHORT_PULSE_DURATION : LONG_PULSE_DURATION);
    digitalWrite(ADB_PIN, HIGH);
    pinMode(ADB_PIN, INPUT_PULLUP);
    delayMicroseconds(b ? LONG_PULSE_DURATION : SHORT_PULSE_DURATION);
}
void sendByte(unsigned char b) {
    for (int i = 0; i < 8; i++) {
        send((b >> (7 - i)) & 1);
    }
}
void talk0() {
    delayMicroseconds(100);
    delayMicroseconds(160);
    send(1);

    unsigned char data0, data1;
    data0 = (buttonState << 7) | (dy & 0x7F);
    data1 = 0x80 | (dx & 0x7F);

    sendByte(data0);
    sendByte(data1);
    send(0);
}
void talk3() {
    delayMicroseconds(100);
    delayMicroseconds(160);
    send(1);

    unsigned char data0, data1;
    myAddress = random(4, 15);
    myAddress = 3;
    data0 = (1 << 6) | (srqEnabled << 5) | myAddress;
    data1 = handlerId;

    sendByte(data0);
    sendByte(data1);
    send(0);
}



void adbStateChanged() {
    diff = TCNT1 >> 1;

    switch (state) {
        case WAITING_FOR_ATTENTION:
            if (diff < 850 && diff > 750) {
                state = WAITING_FOR_SYNC;
            }
            break;
            
        case WAITING_FOR_SYNC:
            if (diff < 75 && diff > 55) {
                state = READING_COMMAND_BITS;
                count = 0;
                command = 0;
                break;
            }
            state = WAITING_FOR_ATTENTION;
            break;
            
        case READING_COMMAND_BITS:
            diffs[count] = diff;
            if (count % 2 == 0 && (count / 2) < 8 && diff < 50) {
                command |= (1 << (7 - (count / 2)));
            }
            if (++count < 16) break;
            
            state = WAITING_FOR_ATTENTION;
            
            int commandType = (command >> 2) & 3;
            switch (commandType) {
                case TALK:
                    if ((command >> 4) == myAddress) {
                        if ((command & 3) == 3) {
                            talk3();
                        } else if ((command & 3) == 0) {
                            if (haveDataToSend) {
                                talk0();
                            }
                        }
                        haveDataToSend = 0;
                        break;
                    }
                    if (haveDataToSend && srqEnabled) {
                        lowPulse(200);
                    }
                    count = 0;
                    
                    break;
                    
                case LISTEN:
                    if ((command >> 4) == myAddress) {
                        count = 0;
                        args = 0;
                        state = READING_COMMAND_ARGS;
                        break;
                    }
                    break;
            }
            break;
            
        case READING_COMMAND_ARGS:
            diffs[count] = diff;
            if (count > 38 || diff > 75) {
                // TODO: Handle LISTEN commands here.
                state = WAITING_FOR_ATTENTION;
            }
            count++;
            break;
    }

    TCNT1 = 0;
}
