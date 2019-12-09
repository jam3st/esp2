#include <mem.h>
#include <stdarg.h>
#include <lwip/netif.h>
#include <lwip/app/espconn_tcp.h>
#include <lwip/app/espconn_udp.h>


#include "user_interface.h"

static bool TestChip = FALSE;
static uint32 ChipId = 0u;

static const uint8 LOW = 0x0;
static const uint8 HIGH = 0x1;

static char PING[] = { 'p', 'i', 'n', 'g' };
static char RING[] = { 'r', 'i', 'n', 'g' };
static char BING[] = { 'b', 'i', 'n', 'g' };

static char const goHome[] = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=0\r\nContent-Type: text/html\r\nX-HttpTimestamp: %08x\r\n\r\n\
<!DOCTYPE html><head></head>\
<body style='background-color:black;'><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></body></html>";

static char const error404[] = "HTTP/1.1 404 OK\r\nConnection: close\r\nCache-Control: max-age=0\r\nContent-Type: text/html\r\n\r\n\
<!DOCTYPE html><head><title>Error</title></head>\
<body style='background-color:black;color:green;'>\
</body></html>";
static char const updadting[] = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=0\r\nContent-Type: text/html\r\nX-HttpTimestamp: %08x\r\n\r\n\
<!DOCTYPE html><head><title>Updating...</title></head>\
<body style='background-color:black;color:green;'>\
<meta http-equiv='refresh' content='10;url=http://192.168.4.1/'>Attempting update from 192.168.1.1. Please wait...\
</body></html>";

static char const idleOptions[] = "\
<p><a href='/open' class='button'>OPEN BOTH</a></p>\
<p><a href='/util' class='button'>OPEN VISITOR</a></p>\
<p><a href='/rand' class='button'>OPEN RAND</a></p>\
<p><a href='/close' class='button'>CLOSE BOTH</a></p>";

static char const webDoorOpenHtml[] = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=0\r\nContent-Type: text/html\r\nX-HttpTimestamp: %08x\r\n\r\n\
<!DOCTYPE html><head></head><meta name='viewport' content='width=device-width, initial-scale=1'><style>a.button {\
-webkit-appearance: button; -moz-appearance: button; appearance: button; text-decoration: none; color: initial;}</style><body style='background-color:black;color:green;'>\
<a href='/stop' class='button'>STOP</a><p/>\
%s\
UT %d:%02d:%02d A %d M %d S %d dB</p>\
<table>\
<th>IR 0</th>\
<th>IR 1</th>\
<th>MG 0</th>\
<th>MG 1</th>\
<th>MG 2</th>\
<th>MG 3</th>\
<tr>\
<td style='background-color:%s'>&nbsp;</td>\
<td style='background-color:%s'>&nbsp;</td>\
<td style='background-color:%s'>&nbsp;</td>\
<td style='background-color:%s'>&nbsp;</td>\
<td style='background-color:%s'>&nbsp;</td>\
<td style='background-color:%s'>&nbsp;</td>\
</tr>\
</table>\
</body></html>";

static os_timer_t ptimer;
static uint32 epoc = 0u;
static uint16 minAdc = 800u;
static bool suppressEastMessage = FALSE;
static bool suppressWestMessage = FALSE;
static bool insideSensorTriggered = FALSE;
static bool frontSensorTriggered = FALSE;
static bool NoWaitForPause = FALSE;

static const uint16 IdleThreshold = 700u;
static const uint16 SingleRunThreshold = 450u;
static const uint16 SingleStartThreshold = 400u;
static const uint16 SingleStartOneRunThreshold = 300u;
static const uint16 BothRunThreshold = 350u;
static const uint16 ShutdownThreshold = 275u;

static uint32 firstTick = 0u;

static uint32 lastTick = 0;
static uint32 lastMasterTick = 0u;
static uint32_t lastMasterPing = 0u;

static uint32_t eastGateEventStartTick = 0u;
static uint32_t westGateEventStartTick = 0u;

static uint32_t openEventStartTick = 0u;
static uint32_t activeEventStartTick = 0u;

static uint32 lastPauseTick = 0u;

static const uint32 PauseTime =   4654321u;

static const uint32 MaxGateOpentime =  0x74000000u;
static const uint32 MaxActiveTime  =   0x7fffffffu;

static const uint32 MaxGateStartTime =    854321u;
static const uint32 MaxWestGateRuntime =    23654321u;
static const uint32 MaxEastGateRuntime =    26654321u;

static const uint32 PingInterval =            10000000u;
static const uint32 NoConnectResetInterval = 300000000u;
static struct espconn *masterConn = NULL;

static uint32 alarmTick = 0u;

static int8 detectionCounter = 0u;


enum {
    EastOpen,
    EastClosed,
    WestOpen,
    WestClosed,
    FrontIr,
    InsideIr,
    NumSensors
};

enum {
    BOTH_GATES = 0u,
    EAST_GATE = 1u,
    WEST_GATE = 2u
};

enum {
    IDLE = 0u,
    PROGRAMMING = (1u << 0u),
    OPENING = (1u << 1u),
    CLOSING = (1u << 2u),
    STARTING_EAST = (1u << 3u),
    STARTING_WEST = (1u << 4u),
    OPENED = (1u << 5u),
    PAUSED = (1u << 6u),
    EAST_ONLY  = (1u << 7u),
    WEST_ONLY = (1u << 8u),
    EAST_ACTIVE = (1u << 9u),
    WEST_ACTIVE = (1u << 10u),
} mainState = IDLE;

static uint32 mainTick = 0u;

void  __attribute__((section(".irom0.text"))) debugLog(char const* const format, ...) {
    static uint32 seq = 0u;
    char* page = os_malloc(1024u);
    size_t len = 0;
    va_list args;
    len = os_snprintf(page, 1024u, "%08x\t", seq);
    va_start(args, format);
    len = len + ets_vsnprintf(page + len, 1024u - len, format, args);
    va_end(args);
    espconn_sendto(masterConn, page, len);
    os_free(page);
    ++seq;
}

void  __attribute__((section(".irom0.text"))) otaCompletedCb(bool result, uint8 slot) {
    if(result) {
        if(rboot_get_current_rom() == 0u) {
            rboot_set_current_rom(1u);
        } else {
            rboot_set_current_rom(0u);
        }
    }
    system_restart();
    for(;;) {
    }
}

void __attribute__((section(".irom0.text"))) user_spi_flash_dio_to_qio_pre_init() {
}


static void eastOff() {
    GPIO_OUTPUT_SET(D9, HIGH);
    GPIO_OUTPUT_SET(D10, HIGH);
}

static void westOff() {
    GPIO_OUTPUT_SET(D3, HIGH);
    GPIO_OUTPUT_SET(D4, HIGH);
}

static void openWest() {
    GPIO_OUTPUT_SET(D3, LOW);
    GPIO_OUTPUT_SET(D4, HIGH);
}

static void closeWest() {
    GPIO_OUTPUT_SET(D3, HIGH);
    GPIO_OUTPUT_SET(D4, LOW);
}

static void openEast() {
    GPIO_OUTPUT_SET(D9, LOW);
    GPIO_OUTPUT_SET(D10, HIGH);
}

static void closeEast() {
    GPIO_OUTPUT_SET(D9, HIGH);
    GPIO_OUTPUT_SET(D10, LOW);
}

static __attribute__((section(".irom0.text"))) uint16 getAndUpdateMedianFilteredAdc(bool reset) {
// Must be odd
    enum { NumAdc =  13u};
    const uint8 MidAdcPos = NumAdc / 2u + 1u; // 8 ~ 40 - 45 ms
    static uint8 lastAdcPos = 0;
    static uint16 AdcVals[NumAdc] = { 0 };
    static uint16 SortedAdcVals[NumAdc];

    uint8 sortedPos = 0u; 
    if(reset) {
        for(uint8 i = 0u; i < sizeof(SortedAdcVals) / sizeof(SortedAdcVals[0]); ++i) {
            AdcVals[i] = 0xffffu;
        }
        return 0xffffu;
    }
    AdcVals[lastAdcPos++] = system_adc_read();
    if(lastAdcPos >= sizeof(AdcVals) / sizeof(AdcVals[0])) {
        lastAdcPos = 0u; 
    }   
    for(uint8 i = 0u; i < sizeof(SortedAdcVals) / sizeof(SortedAdcVals[0]); ++i) {
        SortedAdcVals[i] = AdcVals[i];
    }   
    for(uint8 i = 0u; i < sizeof(SortedAdcVals) / sizeof(SortedAdcVals[0]) - 1; ++i) {
        uint16 iMinPos = i;
        for(uint8 j = i; j < sizeof(SortedAdcVals) / sizeof(SortedAdcVals[0]); ++j) {
            if(SortedAdcVals[j] < SortedAdcVals[iMinPos]) {
                iMinPos = j;
            }
        }
        if(iMinPos != i) {
            uint16 tmp = SortedAdcVals[i];
            SortedAdcVals[i] = SortedAdcVals[iMinPos];
            SortedAdcVals[iMinPos] = tmp;
        }
    }   
    return SortedAdcVals[MidAdcPos];

}


static __attribute__((section(".irom0.text"))) void returnIdle() {
    mainState = IDLE;
    insideSensorTriggered = FALSE;
    frontSensorTriggered = FALSE;
    suppressEastMessage = FALSE;
    suppressWestMessage = FALSE;
    eastOff();
    westOff();
    os_timer_disarm(&ptimer);
    os_timer_arm(&ptimer, 32, 1);
    getAndUpdateMedianFilteredAdc(TRUE);
    debugLog("Returning to idle");
}

static __attribute__((section(".irom0.text"))) void notifyClose(int mask) {
    debugLog("notifyClosewith state %02x mask %d", mainState, mask);
    if(IDLE == mainState) {
        suppressEastMessage = FALSE;
        suppressWestMessage = FALSE;
        insideSensorTriggered = FALSE;
        frontSensorTriggered = FALSE;
        if(BOTH_GATES == mask || EAST_GATE == mask) { 
            mainState = CLOSING | STARTING_EAST;
            if(EAST_GATE == mask) {
                mainState |= EAST_ONLY;
            }
        } else {
            mainState = CLOSING | STARTING_WEST | WEST_ONLY;
        }
        westGateEventStartTick = system_get_time();
        eastGateEventStartTick = system_get_time();
        activeEventStartTick = system_get_time();
        os_timer_disarm(&ptimer);
        os_timer_arm(&ptimer, 5, 1);
        getAndUpdateMedianFilteredAdc(TRUE);
    } else {
        if(EAST_GATE == mask) { 
            mainState = EAST_ONLY;
        } else if(WEST_GATE == mask) {
            mainState = WEST_ONLY;
        } else {
	         mainState = 0;
	    }
        mainState = mainState & ~OPENING;
        mainState = mainState & ~OPENED;
        mainState = mainState | CLOSING;
        mainState = mainState | PAUSED;
        activeEventStartTick = system_get_time();
        NoWaitForPause = TRUE;
        lastPauseTick = system_get_time();
        getAndUpdateMedianFilteredAdc(TRUE);
    }
}

static  __attribute__((section(".irom0.text"))) void notifyOpen(int mask, int8 dc) {
    debugLog("notifyOpen with state %02x mask %d dc %d", mainState, mask, dc);
    if(IDLE == mainState) {
        detectionCounter = dc;
        suppressEastMessage = FALSE;
        suppressWestMessage = FALSE;
        insideSensorTriggered = FALSE;
        frontSensorTriggered = FALSE;
        if(BOTH_GATES == mask || EAST_GATE == mask) { 
            mainState = OPENING | STARTING_EAST;
            if(EAST_GATE == mask) {
                mainState |= EAST_ONLY;
            }
        } else {
            mainState = OPENING | STARTING_WEST | WEST_ONLY;
        }
        westGateEventStartTick = system_get_time();
        eastGateEventStartTick = system_get_time();
        activeEventStartTick = system_get_time();
        os_timer_disarm(&ptimer);
        os_timer_arm(&ptimer, 5, 1);
        getAndUpdateMedianFilteredAdc(TRUE);
    } else {
        if(EAST_GATE == mask) { 
            mainState = EAST_ONLY;
        } else if(WEST_GATE == mask) {
            mainState = WEST_ONLY;
        } else {
	        mainState = 0;
	    }
        detectionCounter = dc;
        mainState = mainState & ~CLOSING;
        mainState = mainState & ~OPENED;
        mainState = mainState | OPENING;
        mainState = mainState | PAUSED;
        activeEventStartTick = system_get_time();
        NoWaitForPause = TRUE;
        lastPauseTick = system_get_time();
        getAndUpdateMedianFilteredAdc(TRUE);
    }
}

inline bool isSensorActive(uint8 const sensors, uint8 const sensor) {
    return (sensors & (1u << sensor)) != 0u;
}

static __attribute__((section(".irom0.text"))) uint8 getSensors() {
    uint32 ret = 0u;
    ret |= (((GPIO_INPUT_GET(D2) == LOW ? 1u : 0u) & 0x1u) << EastOpen);
    ret |= (((GPIO_INPUT_GET(D7) == LOW ? 1u : 0u) & 0x1u) << EastClosed);
    ret |= (((GPIO_INPUT_GET(D5) == LOW ? 1u : 0u) & 0x1u) << WestOpen);
    ret |= (((GPIO_INPUT_GET(D6) == LOW ? 1u : 0u) & 0x1u) << WestClosed);
    ret |= (((GPIO_INPUT_GET(D1) == LOW ? 1u : 0u) & 0x1u) << InsideIr);
    ret |= (((gpio16_input_get() == LOW ? 1u : 0u) & 0x1u) << FrontIr);
    return ret;
}

static __attribute__((section(".irom0.text"))) uint8 getDebouncedFilteredSensors() {
// Must be odd
    enum { NumFilt = 15u};
    const uint8 MidSetCountThreshold =  6 * NumFilt / 7u + 1u;
    const uint8 IrCountThreshold = 3; // Safety first
    static uint8 lastSensorPos = 0;
    static uint8 sensors[NumFilt] = { 0 };
    uint8 counts[NumSensors] = { 0x0u };
    uint8 ret = 0x0u;
    sensors[lastSensorPos++] = getSensors();
    if(lastSensorPos >= sizeof(sensors) / sizeof(sensors[0])) {
        lastSensorPos = 0u; 
    }   
    for(uint8 i = 0u; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        counts[i] = 0u;
    }   
    for(uint8 i = 0u; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
        if((sensors[i] & (1u << EastOpen)) != 0x0u) { counts[EastOpen]++; }
        if((sensors[i] & (1u << EastClosed)) != 0x0u) { counts[EastClosed]++; }
        if((sensors[i] & (1u << WestOpen)) != 0x0u) { counts[WestOpen]++; }
        if((sensors[i] & (1u << WestClosed)) != 0x0u) { counts[WestClosed]++; }
        if((sensors[i] & (1u << InsideIr)) != 0x0u) { counts[InsideIr]++; }
        if((sensors[i] & (1u << FrontIr)) != 0x0u) { counts[FrontIr]++; }
    }   
    ret = 0x0u;
    for(uint8 i = 0u; i < InsideIr; ++i) {
        if(counts[i] >= MidSetCountThreshold) {
            ret |= (0x1u << i);
        }
    }   
    if(counts[InsideIr] >= IrCountThreshold) { 
        ret |= (0x1u << InsideIr);
    }
    if(counts[FrontIr] >= IrCountThreshold) { 
        ret |= (0x1u << FrontIr);
    }
    return ret;
}

static __attribute__((section(".irom0.text"))) void currentCheck(uint16 const adc) {
    bool eastStarting = (STARTING_EAST & mainState) == STARTING_EAST;
    bool westStarting = (STARTING_WEST & mainState) == STARTING_WEST;
    bool eastRunning = (EAST_ACTIVE & mainState) == EAST_ACTIVE;
    bool westRunning = (WEST_ACTIVE & mainState) == WEST_ACTIVE;

    if(adc < ShutdownThreshold) {
        debugLog("Danger: Overcurrent at %d %02x", adc, mainState);
        returnIdle();
    } else if(eastStarting && westStarting) {
        debugLog("Danger: Don't start two at once %d %02x", adc, mainState);
        returnIdle();
    } else if(eastStarting && !westRunning && adc < SingleStartThreshold) {
        debugLog("Danger: East starting %d %02x", adc, mainState);
        returnIdle();
    } else if(westStarting && !eastRunning && adc < SingleStartThreshold) {
        debugLog("Dange: West starting %d %02x", adc, mainState);
        returnIdle();
    } else if(eastStarting && westRunning && adc < SingleStartOneRunThreshold) {
        debugLog("Danger: East starting West running %d %02x", adc, mainState);
        returnIdle();
    } else if(westStarting && eastRunning && adc < SingleStartOneRunThreshold) {
        debugLog("Danger: West starting East running %d %02x", adc, mainState);
        returnIdle();
    } else if(eastRunning && westRunning && !(eastStarting || westStarting) && adc < BothRunThreshold) {
        debugLog("Danger: West running East running %d %02x", adc, mainState);
        returnIdle();
    } else if(!eastRunning && !westRunning) {
        if(adc < IdleThreshold) {
            returnIdle();
            debugLog("Check adc connection. Expecting idle current, got %d %02x", adc, mainState);
        }
    } 
    if(minAdc > adc) {
        minAdc = adc;
        debugLog("New max current %d %02x", adc, mainState);
    }

}

static __attribute__((section(".irom0.text"))) void transitionToOpen(uint32 const currMsTick) {
    debugLog("Opened waiting for movement or timeout");
    activeEventStartTick = currMsTick;
    eastGateEventStartTick = currMsTick;
    westGateEventStartTick = currMsTick;
    if((mainState & EAST_ONLY) == EAST_ONLY) {
        mainState = OPENED | EAST_ONLY;
    } else if((mainState & EAST_ONLY) == WEST_ONLY) {
        mainState = OPENED | WEST_ONLY;
    } else {
        mainState = OPENED;
    }
    getAndUpdateMedianFilteredAdc(TRUE);
}

static __attribute__((section(".irom0.text"))) void processState(uint32 const currMsTick, uint8 const sensors, uint16 const adc) {
    bool eastOpen = FALSE;
    bool westOpen = FALSE;
    bool eastClose = FALSE;
    bool westClose = FALSE;
    bool impeded = FALSE;
    u32 prevMainstate = mainState;
    const u32 activeMask = (EAST_ACTIVE | WEST_ACTIVE);

    if(insideSensorTriggered && !frontSensorTriggered && isSensorActive(sensors, FrontIr)) {
        debugLog("Detected someone leaving %d %02x", detectionCounter, mainState);
        mainState = mainState & ~(EAST_ONLY | WEST_ONLY | OPENING | OPENED | activeMask);
        NoWaitForPause = TRUE;
        --detectionCounter;
        if(detectionCounter <= 0) { 
            mainState = mainState | CLOSING;
        } else {
            transitionToOpen(currMsTick);
        }
    }
    if(frontSensorTriggered && !insideSensorTriggered && isSensorActive(sensors, InsideIr)) {
        debugLog("Detected someone entering %d %02x", detectionCounter, mainState);
        mainState = mainState & ~(EAST_ONLY | WEST_ONLY | OPENING | OPENED | activeMask);
        --detectionCounter;
        if(detectionCounter <= 0) {
            mainState = mainState | CLOSING;
        } else {
            transitionToOpen(currMsTick);
        }
    }

    if(isSensorActive(sensors, InsideIr)) {
        insideSensorTriggered = TRUE;
        impeded = TRUE;
        mainState = mainState | PAUSED;
    }
    if(isSensorActive(sensors, FrontIr)) {
        frontSensorTriggered = TRUE;
        impeded = TRUE;
        mainState = mainState | PAUSED;
    }

    if(!isSensorActive(sensors, InsideIr) && !isSensorActive(sensors, FrontIr) &&
        insideSensorTriggered && frontSensorTriggered) {
        debugLog("Nobody in detection area %02x", mainState);
        insideSensorTriggered = FALSE;
        frontSensorTriggered = FALSE;
        NoWaitForPause = TRUE;
    }

    if(impeded) {
        lastPauseTick = currMsTick;
    } else if((PAUSED & mainState) == PAUSED) {
        uint32 elapsed = currMsTick - lastPauseTick;
        uint32 remaining = PauseTime - elapsed;
        if(NoWaitForPause || (remaining & 0x80000000u) != 0x0u) {
            debugLog("Pause timer expired state is %02x", mainState);
            suppressEastMessage = FALSE;
            suppressWestMessage = FALSE;
            insideSensorTriggered = FALSE;
            frontSensorTriggered = FALSE;
            eastGateEventStartTick = currMsTick;
            westGateEventStartTick = currMsTick;
            mainState = mainState & ~(STARTING_WEST | STARTING_WEST | PAUSED);
            if((mainState & WEST_ONLY) == WEST_ONLY) {
                mainState |= STARTING_WEST;
            } else {
                mainState |= STARTING_EAST;
            }
            NoWaitForPause = FALSE;
            mainState = mainState & ~(activeMask);
            getAndUpdateMedianFilteredAdc(TRUE);
        }
    } else if((OPENED & mainState) == OPENED) {
        uint32 elapsed = currMsTick - openEventStartTick;
        uint32 remaining = MaxGateOpentime - elapsed;
        if((remaining & 0x80000000u) != 0x0u) {
            debugLog("Timeout waiting for someone to cross");
            if((mainState & EAST_ONLY) == EAST_ONLY) {
                mainState = CLOSING | STARTING_EAST | EAST_ONLY;
            } else if((mainState & WEST_ONLY) == WEST_ONLY) {
                mainState = CLOSING | STARTING_WEST | WEST_ONLY;
            } else {
                mainState = CLOSING | STARTING_EAST;
            }
            eastGateEventStartTick = currMsTick;
            westGateEventStartTick = currMsTick;
            suppressEastMessage = FALSE;
            suppressWestMessage = FALSE;
            mainState = mainState & ~(activeMask);
            getAndUpdateMedianFilteredAdc(TRUE);
        }
    } else if((STARTING_EAST & mainState) == STARTING_EAST) { 
        uint32 elapsed = currMsTick - eastGateEventStartTick;
        uint32 remaining = MaxGateStartTime - elapsed;
        if((remaining & 0x80000000u) != 0x0u) {
            if(adc < SingleRunThreshold) { 
                debugLog("Timeout waiting for east to start adc %d", adc);
                returnIdle();
                return;
            } else {
                debugLog("East has started with adc at %d", adc);
                eastGateEventStartTick = currMsTick;
                westGateEventStartTick = currMsTick;
                mainState = mainState & ~STARTING_EAST;
                if((mainState & EAST_ONLY) == 0u) {
                    mainState = mainState | STARTING_WEST;
                }
                if((OPENING & mainState) == OPENING) { 
                    eastOpen = TRUE;
                } else if((CLOSING & mainState) == CLOSING) {
                    eastClose = TRUE;
                }
                mainState = mainState & ~(activeMask);
                mainState = mainState & ~STARTING_EAST;
                getAndUpdateMedianFilteredAdc(TRUE);
            }
        } else {
                if((OPENING & mainState) == OPENING) { 
                    eastOpen = TRUE;
                } else if((CLOSING & mainState) == CLOSING) {
                    eastClose = TRUE;
                }
        }
    } else if((STARTING_WEST & mainState) == STARTING_WEST) { 
        uint32 elapsed = currMsTick - westGateEventStartTick;
        uint32 remaining = MaxGateStartTime - elapsed;
        if((remaining & 0x80000000u) != 0x0u) {
            if(((mainState & WEST_ONLY) == WEST_ONLY || (mainState & EAST_ACTIVE) == 0u) && adc < SingleRunThreshold 
                || adc < SingleStartOneRunThreshold) { 
                debugLog("Timeout waiting for west to start %d", adc);
                returnIdle();
                return;
            } else {
                if((mainState & WEST_ONLY) == WEST_ONLY) { 
                    debugLog("West has started with adc at %d", adc);
                } else {
                    debugLog("West has started with East might be running and adc at %d", adc);
                }
                westGateEventStartTick = currMsTick;
                mainState = mainState & ~STARTING_WEST;
                getAndUpdateMedianFilteredAdc(TRUE);
            }
        } 
        if((OPENING & mainState) == OPENING) { 
            westOpen = TRUE;
            if((mainState & WEST_ONLY) == 0u) {
                eastOpen = TRUE;
            }
        } else if((CLOSING & mainState) == CLOSING) {
            westClose = TRUE;
            if((mainState & WEST_ONLY) == 0u) {
                eastClose = TRUE;
            }
        }
    } else if((OPENING & mainState) == OPENING ) {
        uint32 elapsed = currMsTick - westGateEventStartTick;
        uint32 remaining = MaxWestGateRuntime - elapsed;
        if(isSensorActive(sensors, WestOpen)) {
            if(!suppressWestMessage) {
                debugLog("West has opened");
                suppressWestMessage = TRUE;
            }
        } else if((remaining & 0x80000000u) == 0x0u) {
           westOpen = TRUE;
        } else {
            if(!suppressWestMessage) {
                debugLog("Timeout opening west gate");
                suppressWestMessage = TRUE;
            }
        }
        elapsed = currMsTick - eastGateEventStartTick;
        remaining = MaxEastGateRuntime - elapsed;
        if(isSensorActive(sensors, EastOpen)) {
            if(!suppressEastMessage) {
                debugLog("East has opened");
                suppressEastMessage = TRUE;
            }
        } else if((remaining & 0x80000000u) == 0x0u) {
            eastOpen = TRUE;
        } else {
            if(!suppressEastMessage) {
                debugLog("Timeout opening east gate");
                suppressEastMessage = TRUE;
            }
        }
        if( (((mainState & EAST_ONLY) == EAST_ONLY) && !eastOpen) ||
            (((mainState & WEST_ONLY) == WEST_ONLY) && !westOpen) ||
            (!eastOpen && !westOpen) ) {
                transitionToOpen(currMsTick);
        }
    } else if((CLOSING & mainState) == CLOSING) {
        uint32 elapsed = currMsTick - westGateEventStartTick;
        uint32 remaining = MaxWestGateRuntime - elapsed;
        if(isSensorActive(sensors, WestClosed)) {
            if(!suppressWestMessage) {
                debugLog("West has closed");
                suppressWestMessage = TRUE;
            }
        } else if((remaining & 0x80000000u) == 0x0u) {
            westClose = TRUE;
        } else {
            if(!suppressWestMessage) {
                debugLog("Timeout closing west gate");
                suppressWestMessage = TRUE;
            }
        }
        elapsed = currMsTick - eastGateEventStartTick;
        remaining = MaxEastGateRuntime - elapsed;
        if(isSensorActive(sensors, EastClosed)) {
            if(!suppressEastMessage) {
                debugLog("East has closed");
                suppressEastMessage = TRUE;
            }
        } else if((remaining & 0x80000000u) == 0x0u) {
            eastClose = TRUE;
        } else {
            if(!suppressEastMessage) {
                debugLog("Timeout closing east gate");
                suppressEastMessage = TRUE;
            }
        }
        if(((mainState & EAST_ONLY) == EAST_ONLY) && !eastClose) {
            debugLog("East gate closed");
            returnIdle();
            return;
        }

        if(((mainState & WEST_ONLY) == WEST_ONLY) && !westClose) {
            debugLog("west gate closed");
            returnIdle();
            return;
        }

        if(!eastClose && !westClose) {
            debugLog("Both gates closed");
            returnIdle();
            return;
        }
     } 

    if((eastClose && westOpen) || (eastOpen && westClose)) {
        debugLog("Cannot open and close simultaneosly. Aborting");
        returnIdle();
        return;
    }
    if(westClose && westOpen) {
        debugLog("Cannot open and close west. Aborting");
        returnIdle();
    }

    if(eastClose && eastOpen) {
        debugLog("Cannot open and close east. Aborting");
        returnIdle();
        return;
    }
    if(westClose && westOpen) {
        debugLog("Cannot open and close west. Aborting");
        returnIdle();
        return;
    }

    if(westOpen && isSensorActive(sensors, WestOpen)) {
        westOpen = FALSE;
    }

    if(westClose && isSensorActive(sensors, WestClosed)) {
        westClose = FALSE;
    }

    if(eastOpen && isSensorActive(sensors, EastOpen)) {
        eastOpen = FALSE;
    }

    if(eastClose && isSensorActive(sensors, EastClosed)) {
        eastClose = FALSE;
    }

    if((mainState & WEST_ONLY) == WEST_ONLY) {
        mainState = mainState & ~(EAST_ACTIVE);
        eastOff();
    } else if(eastOpen) {
        mainState = mainState | EAST_ACTIVE;
        openEast();
    } else if(eastClose) {
        mainState = mainState | EAST_ACTIVE;
        closeEast();
    } else {
        mainState = mainState & ~(EAST_ACTIVE);
        eastOff();
    }

    if((mainState & EAST_ONLY) == EAST_ONLY) {
        mainState = mainState & ~(WEST_ACTIVE);
        westOff();
    } else if(westOpen) {
        mainState = mainState | WEST_ACTIVE;
        openWest();
    } else if(westClose) {
        mainState = mainState | WEST_ACTIVE;
        closeWest();
    } else {
        mainState = mainState & ~(WEST_ACTIVE);
        westOff();
    }
    if((activeMask & prevMainstate) != (activeMask & mainState)) {
        getAndUpdateMedianFilteredAdc(TRUE);
    }
}

static bool alarmActive() {
    return GPIO_INPUT_GET(D1) == LOW || gpio16_input_get() == LOW;
}

void user_pre_init() {
    static const partition_item_t at_partition_table[] = {
            { SYSTEM_PARTITION_BOOTLOADER,                      0x0,         0x1000},
            { SYSTEM_PARTITION_OTA_1,                           0x1000,     0x6A000},
            { SYSTEM_PARTITION_OTA_2,                           0x81000,    0x6A000},
            { SYSTEM_PARTITION_RF_CAL,                          0x3fb000,    0x1000},  // 1024 - 5 sectors of 4096
            { SYSTEM_PARTITION_PHY_DATA,                        0x3fc000,    0x1000},
            { SYSTEM_PARTITION_SYSTEM_PARAMETER,                0x3fd000,    0x3000}
    };

    static const unsigned long SPI_FLASH_SIZE_MAP = 4u;
    if(!system_partition_table_regist(at_partition_table, sizeof(at_partition_table)/sizeof(at_partition_table[0]), SPI_FLASH_SIZE_MAP)) {
        for(;;) {
            os_printf("Invalid partition table\r\n");
        }
    }
}


static __attribute__((section(".irom0.text"))) void web_config_client_recv_cb(void *arg, char *data, unsigned short length)
{
    struct espconn *pespconn = (struct espconn *)arg;
    size_t pageLen = sizeof(webDoorOpenHtml) + sizeof(idleOptions) + 128u;
    char* page = os_malloc(pageLen);
    size_t len = 0;
    char remoteIpAndPort[24];

    len = os_snprintf(page, pageLen, goHome, system_get_time());
    os_snprintf(remoteIpAndPort, sizeof(remoteIpAndPort), "%d.%d.%d.%d:%d", pespconn->proto.tcp->remote_ip[0],
                                 pespconn->proto.tcp->remote_ip[1], pespconn->proto.tcp->remote_ip[2],
                                 pespconn->proto.tcp->remote_ip[3], pespconn->proto.tcp->remote_port);

    if(data == strstr(data, "GET /stop ")) {
        returnIdle();
        debugLog("Got stop from %s", remoteIpAndPort);
    } else if(data == strstr(data, "GET /reset ")) {
        returnIdle();
        debugLog("Got reboot.");
        system_restart();
        for(;;) {
        }
    } else if(data == strstr(data, "GET /open ")) {
        debugLog("Got open from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyOpen(BOTH_GATES, 0);
    } else if(data == strstr(data, "GET /util ")) {
        debugLog("Got util from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyOpen((os_random() & 0x1u) == 0u ? EAST_GATE : WEST_GATE, 2);
    } else if(data == strstr(data, "GET /rand ")) {
        debugLog("Got rand from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyOpen((os_random() & 0x1u) == 0u ? EAST_GATE : WEST_GATE, 0);
    } else if(IDLE == mainState && data == strstr(data, "GET /openeast ")) {
        debugLog("Got open east from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyOpen(EAST_GATE, 0);
    } else if(IDLE == mainState && data == strstr(data, "GET /openwest ")) {
        debugLog("Got open west from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyOpen(WEST_GATE, 0);
    } else if(IDLE == mainState && data == strstr(data, "GET /close ")) {
        debugLog("Got close from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyClose(BOTH_GATES);
    } else if(IDLE == mainState && data == strstr(data, "GET /closeeast ")) {
        debugLog("Got close east from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyClose(EAST_GATE);
    } else if(IDLE == mainState && data == strstr(data, "GET /closewest ")) {
        debugLog("Got close west from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        notifyClose(WEST_GATE);
    } else if(IDLE == mainState && data == strstr(data, "GET /update ")) {
        debugLog("Got update from %s", remoteIpAndPort);
        espconn_sendto(masterConn, BING, sizeof(BING));
        returnIdle();
        len = os_snprintf(page, pageLen, updadting, system_get_time());
        espconn_send(pespconn, page, len);
        os_free(page);
        rboot_ota_start(otaCompletedCb);
        return;
    } else if(data == strstr(data, "GET / ")) {
        uint8 mins = 0u;
        uint8 secs = 0u;
        uint32 hours = 0u;
        uint32 currTick = system_get_time();
        uint64 ticks = (((uint64)epoc) * (1ull << 32)) + (uint64)((uint32)(currTick - firstTick));

        secs = (ticks /  1000000ull) % 60u;
        mins = ((ticks / 1000000ull) / 60u) % 60u;
        hours = ticks /  1000000ull / 3600;
        len = os_snprintf(page, pageLen, webDoorOpenHtml,
                         currTick,
                         IDLE == mainState ? idleOptions  : "",
                         hours, mins, secs,
                         system_adc_read(), minAdc,
                         wifi_station_get_rssi(),
                         gpio16_input_get() ? "red" : "green",
                         GPIO_INPUT_GET(D1) ? "red" : "green",
                         GPIO_INPUT_GET(D2) ? "red" : "green",
                         GPIO_INPUT_GET(D7) ? "red" : "green",
                         GPIO_INPUT_GET(D6) ? "red" : "green",
                         GPIO_INPUT_GET(D5) ? "red" : "green"
                         );
    } else {
        len = os_snprintf(page, pageLen, error404, system_get_time());
    }
    espconn_send(pespconn, page, len);
    os_free(page);
}

static __attribute__((section(".irom0.text"))) void web_config_client_reconnect_cb(void *arg, sint8 err) {
    struct espconn *pespconn = (struct espconn *)arg;
    char remoteIpAndPort[24];

    os_snprintf(remoteIpAndPort, sizeof(remoteIpAndPort), "%d.%d.%d.%d:%d", pespconn->proto.tcp->remote_ip[0],
                                 pespconn->proto.tcp->remote_ip[1], pespconn->proto.tcp->remote_ip[2],
                                 pespconn->proto.tcp->remote_ip[3], pespconn->proto.tcp->remote_port);
    debugLog("ReDisconnecting %s err %d state %d", remoteIpAndPort, err,  pespconn->state);
    espconn_disconnect(pespconn);
}

static __attribute__((section(".irom0.text"))) void web_config_client_sent_cb(void *arg) {
    struct espconn *pespconn = (struct espconn *)arg;
    char remoteIpAndPort[24];

    os_snprintf(remoteIpAndPort, sizeof(remoteIpAndPort), "%d.%d.%d.%d:%d", pespconn->proto.tcp->remote_ip[0],
                                 pespconn->proto.tcp->remote_ip[1], pespconn->proto.tcp->remote_ip[2],
                                 pespconn->proto.tcp->remote_ip[3], pespconn->proto.tcp->remote_port);
    debugLog("Disconnecting %s state %d", remoteIpAndPort, pespconn->state);
    espconn_disconnect(pespconn);
}

static __attribute__((section(".irom0.text"))) void web_config_client_disconnect_cb(void *arg) {
    struct espconn *pespconn = (struct espconn *)arg;
    char remoteIpAndPort[24];

    os_snprintf(remoteIpAndPort, sizeof(remoteIpAndPort), "%d.%d.%d.%d:%d", pespconn->proto.tcp->remote_ip[0],
                                 pespconn->proto.tcp->remote_ip[1], pespconn->proto.tcp->remote_ip[2],
                                 pespconn->proto.tcp->remote_ip[3], pespconn->proto.tcp->remote_port);
    debugLog("Disconnected %s state %d", remoteIpAndPort, pespconn->state);
    espconn_disconnect(pespconn);
}

static __attribute__((section(".irom0.text"))) void web_config_client_connected_cb(void *arg) {
    struct espconn *pespconn = (struct espconn *)arg;
    char remoteIpAndPort[24];

    os_snprintf(remoteIpAndPort, sizeof(remoteIpAndPort), "%d.%d.%d.%d:%d", pespconn->proto.tcp->remote_ip[0],
                                 pespconn->proto.tcp->remote_ip[1], pespconn->proto.tcp->remote_ip[2],
                                 pespconn->proto.tcp->remote_ip[3], pespconn->proto.tcp->remote_port);
    debugLog("Connection from %s", remoteIpAndPort);
    espconn_regist_recvcb(pespconn,     web_config_client_recv_cb);
    espconn_regist_sentcb(pespconn,     web_config_client_sent_cb);
    espconn_regist_disconcb(pespconn,   web_config_client_disconnect_cb);
    espconn_regist_reconcb(pespconn,    web_config_client_reconnect_cb);
}

static __attribute__((section(".irom0.text"))) void masterRecvData(void *arg, char *data, unsigned short length) {
    length = length & 0xfffc;
    while(length != 0) {
        if(data[0] == 'a' && data[1] == 'l' && data[2] == 'l' && data[3] == 'o') {
            lastMasterPing = system_get_time();
            espconn_sendto(masterConn, BING, sizeof(BING));
        }

        if(data[0] == 's' && data[1] == 't' && data[2] == 'o' && data[3] == 'p') {
            lastMasterTick = system_get_time();
            debugLog("Got stop.");
            returnIdle();
        }
        if(data[0] == 'p' && data[1] == 'o' && data[2] == 'n' && data[3] == 'g') {
            lastMasterTick = system_get_time();
        }

        if(data[0] == 's' && data[1] == 'i' && data[2] == 'n' && data[3] == 'g') {
            lastMasterTick = system_get_time();
        }

        if(data[0] == 'o' && data[1] == 'p' && data[2] == 'e' && data[3] == 'n') {
            lastMasterTick = system_get_time();
            debugLog("Got open both.");
            notifyOpen(BOTH_GATES, 0);
        }

        if(data[0] == 'c' && data[1] == 'l' && data[2] == 's' && data[3] == 'e') {
            lastMasterTick = system_get_time();
            debugLog("Got close both.");
            notifyClose(BOTH_GATES);
        }

        if(data[0] == 'o' && data[1] == 'p' && data[2] == 'e' && data[3] == 'a') {
            lastMasterTick = system_get_time();
            debugLog("Got open east.");
            notifyOpen(EAST_GATE, 0);
        }

        if(data[0] == 'c' && data[1] == 'l' && data[2] == 'e' && data[3] == 'a') {
            lastMasterTick = system_get_time();
            debugLog("Got close east.");
            notifyClose(EAST_GATE);
        }

        if(data[0] == 'o' && data[1] == 'p' && data[2] == 'w' && data[3] == 'e') {
            lastMasterTick = system_get_time();
            debugLog("Got open west.");
            notifyOpen(WEST_GATE, 0);
        }

        if(data[0] == 'c' && data[1] == 'l' && data[2] == 'w' && data[3] == 'e') {
            lastMasterTick = system_get_time();
            debugLog("Got close west.");
            notifyClose(WEST_GATE);
        }

        if(data[0] == 'r' && data[1] == 'a' && data[2] == 'n' && data[3] == 'd') {
            lastMasterTick = system_get_time();
            debugLog("Got west.");
            notifyOpen((os_random() & 0x1u) == 0u ? EAST_GATE : WEST_GATE, 0);
        }

        if(data[0] == 'u' && data[1] == 't' && data[2] == 'i' && data[3] == 'l') {
            lastMasterTick = system_get_time();
            debugLog("Got util.");
            notifyOpen((os_random() & 0x1u) == 0u ? EAST_GATE : WEST_GATE, 2);
        }

        if(data[0] == 'u' && data[1] == 'p' && data[2] == 'd' && data[3] == 'a') {
            lastMasterTick = system_get_time();
            returnIdle();
            debugLog("Got update.");
            rboot_ota_start(otaCompletedCb);
        }

        if(data[0] == 'b' && data[1] == 'o' && data[2] == 'o' && data[3] == 't') {
            lastMasterTick = system_get_time();
            returnIdle();
            system_restart();
            for(;;) {
            }
        }

        length -= 4;
        data += 4;
    }
}


static void connectMaster() {
    ip_addr_t masterIp;

    IP4_ADDR(&masterIp, 192, 168, 1, 1);
    if(masterConn != NULL) {
        espconn_delete(masterConn);
        os_free(masterConn->proto.udp);
        os_free(masterConn);
        masterConn = NULL;
    }

    lastMasterTick = system_get_time();
    masterConn = (struct espconn *)os_zalloc(sizeof(struct espconn));
    masterConn->type  = ESPCONN_UDP;
    masterConn->state = ESPCONN_NONE;
    masterConn->proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
    masterConn->proto.udp->local_port = espconn_port();
    masterConn->proto.udp->remote_port = 1024;
    os_memcpy(masterConn->proto.udp->remote_ip, &masterIp, sizeof(masterConn->proto.udp->remote_ip));

    espconn_create(masterConn);
    espconn_regist_recvcb(masterConn,  masterRecvData);
    debugLog("Time %d:%08x", epoc, system_get_time());
}


static void wifi_event_cb(System_Event_t *evt)
{
    switch (evt->event) {
        case EVENT_STAMODE_CONNECTED:
            connectMaster();
            break;
        case EVENT_STAMODE_DISCONNECTED:
            break;

        case EVENT_STAMODE_AUTHMODE_CHANGE:
            break;

        case EVENT_STAMODE_GOT_IP:
            break;

        case EVENT_SOFTAPMODE_STACONNECTED:
            break;

        case EVENT_SOFTAPMODE_STADISCONNECTED:
            break;

        default:
            break;
    }
}


static void createTcpServer(uint16_t const port, espconn_connect_callback connectCb) {
    struct espconn *conn;
    conn = (struct espconn *)os_zalloc(sizeof(struct espconn));
    conn->type  = ESPCONN_TCP;
    conn->state = ESPCONN_LISTEN;
    conn->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    conn->proto.tcp->local_port = port,

    espconn_regist_connectcb(conn, connectCb);
    espconn_tcp_set_max_con_allow(conn, 2);
    espconn_accept(conn);

}

static void timerCb(void* arg) {
    uint32 currMsTick = system_get_time();
    uint16 adc = 0xffffu;
    static uint8 prevSensors = 0xffu;
    uint8 currSensors = 0xffu;

    if(lastTick <= firstTick && currMsTick > firstTick) {
        ++epoc;
        debugLog("New epoc %d:%08x", epoc, currMsTick);
    }
    lastTick = currMsTick;

    currSensors = getDebouncedFilteredSensors();
    if(prevSensors != currSensors) {
        debugLog("Sensor change was %02x is %02x", prevSensors, currSensors);
    }
    prevSensors = currSensors;

    adc = getAndUpdateMedianFilteredAdc(FALSE);
    currentCheck(adc);

    if(currMsTick - lastMasterPing > PingInterval && masterConn != NULL) {
        if(masterConn != NULL) {
            lastMasterPing = currMsTick;
            espconn_sendto(masterConn, PING, sizeof(PING ));
        }
    }


    if(IDLE == mainState) {
        if(currMsTick - lastMasterTick > NoConnectResetInterval) {
            system_restart();
            for(;;) {
            }
        }
        if(isSensorActive(currSensors, InsideIr) || isSensorActive(currSensors, FrontIr)) {
            if(masterConn != NULL) {
                if(isSensorActive(currSensors, InsideIr)) { 
                    debugLog("Sent ring from inside");
                } 
                if(isSensorActive(currSensors, FrontIr)) { 
                    debugLog("Sent ring from front");
                }
                if(isSensorActive(currSensors, FrontIr)) {
                    espconn_sendto(masterConn, RING, sizeof(RING));
                }
            }
        }
    } else {
        uint32 elapsed = currMsTick - activeEventStartTick;
        uint32 remaining = MaxActiveTime - elapsed;
        if((remaining & 0x80000000u) != 0x0u) {
            debugLog("Timed out waiting for things to happen %08x %08x %08x", currMsTick, elapsed, remaining);
            returnIdle();
        } else {
            processState(currMsTick, currSensors, adc);
        }
    }
}


static void configurePin(uint8 pinNo) {
    uint32 gpio;
    uint32 func;
    switch(pinNo) {
        case D1:
            gpio = FUNC_GPIO5;
            func = PERIPHS_IO_MUX_GPIO5_U;
            break;
        case D2:
            gpio = FUNC_GPIO4;
            func = PERIPHS_IO_MUX_GPIO4_U;
            break;
        case D3:
            gpio = FUNC_GPIO0;
            func = PERIPHS_IO_MUX_GPIO0_U;
            break;
        case D4:
            gpio = FUNC_GPIO2;
            func = PERIPHS_IO_MUX_GPIO2_U;
            break;
        case D5:
            gpio = FUNC_GPIO14;
            func = PERIPHS_IO_MUX_MTMS_U;
            break;
        case D6:
            gpio = FUNC_GPIO12;
            func = PERIPHS_IO_MUX_MTDI_U;
            break;
        case D7:
            gpio = FUNC_GPIO13;
            func = PERIPHS_IO_MUX_MTCK_U;
            break;
        case D8:
            gpio = FUNC_GPIO15;
            func = PERIPHS_IO_MUX_MTDO_U;
            break;
         case D9:
            gpio = FUNC_GPIO3;
            func = PERIPHS_IO_MUX_U0RXD_U;
            break;
        case D10:
            gpio = FUNC_GPIO1;
            func = PERIPHS_IO_MUX_U0TXD_U;
            break;
        default:
            return;
    }
    PIN_FUNC_SELECT(func, gpio);
}

static void initGpios() {
    uint8 i;
    uint8 const INPUTS[] = { D1, D2, D5, D6, D7, D8 };
    uint8 const OUTPUTS[] = { D3, D4, D9, D10 };

    gpio_init();

    for(i = 0u; i < sizeof(INPUTS)/sizeof(INPUTS[0]); ++i) {
        configurePin(INPUTS[i]);
        GPIO_DIS_OUTPUT(INPUTS[i]);
    }

    gpio16_input_conf();

    for(i = 0u; i < sizeof(OUTPUTS)/sizeof(OUTPUTS[0]); ++i) {
        configurePin(OUTPUTS[i]);
        GPIO_OUTPUT_SET(OUTPUTS[i], HIGH);
    }
}

void user_init() {
    struct ip_info info;
    ip_addr_t dnsServerIp;
    struct softap_config apConfig;
    struct station_config stationConf;
    uint8_t mac[6];

    ChipId =  system_get_chip_id();
    TestChip = 0x00eea99f == ChipId;

    system_set_os_print(0);
    os_get_random(mac, 6);
    mac[0] &= 0xfe;
    wifi_set_macaddr(STATION_IF, mac);
    os_get_random(mac, 6);
    mac[0] &= 0xfe;

    wifi_set_macaddr(SOFTAP_IF, mac);
    system_phy_set_powerup_option(3);
    wifi_set_listen_interval(1);
    system_phy_set_max_tpw(82);
    initGpios();

    wifi_set_opmode(STATIONAP_MODE);
    wifi_station_set_auto_connect(1);
    wifi_station_set_hostname(HOSTNAME);

    wifi_station_dhcpc_stop();
    os_memset(&stationConf, 0, sizeof(stationConf));
    os_memset(stationConf.ssid, 0, sizeof(stationConf.ssid));
    os_memcpy(stationConf.ssid, WIFI_SSID, sizeof(WIFI_SSID));
    if(TestChip) {
        stationConf.ssid[sizeof(WIFI_SSID) - 2u] = '7';
    }
    os_memset(stationConf.password, 0, sizeof(stationConf.password));
    os_memcpy(stationConf.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    stationConf.bssid_set = 0;
    stationConf.bssid_set = 0;
    stationConf.threshold.authmode = AUTH_WPA2_PSK;
    stationConf.open_and_wep_mode_disable = 1;
    wifi_station_set_config(&stationConf);
    if(TestChip) {
        IP4_ADDR(&info.ip, 192, 168, 11, 2);
        IP4_ADDR(&info.gw, 192, 168, 11, 1);
    } else {
        IP4_ADDR(&info.ip, 192, 168, 1, 133);
        IP4_ADDR(&info.gw, 192, 168, 1, 129);
    }
    IP4_ADDR(&info.netmask, 255, 255, 255, 192);
    IP4_ADDR(&dnsServerIp, 9, 9, 9, 9);
    wifi_set_ip_info(STATION_IF, &info);

    wifi_softap_get_config(&apConfig);
    os_memset(apConfig.ssid, 0, sizeof(apConfig.ssid));
    os_memcpy(apConfig.ssid, WIFI_AP_SSID, sizeof(WIFI_AP_SSID));
    if(TestChip) {
        apConfig.ssid[sizeof(WIFI_AP_SSID) - 2u] = '0';
    }
    os_memset(apConfig.password, 0, sizeof(apConfig.password));
    os_memcpy(apConfig.password, WIFI_AP_PASSWORD, sizeof(WIFI_AP_PASSWORD));
    apConfig.authmode = AUTH_WPA2_PSK;
    apConfig.beacon_interval = 256;
    apConfig.channel = TestChip ? 1 : 7;
    apConfig.ssid_len = strlen(apConfig.ssid);
    apConfig.max_connection = 8;
    apConfig.ssid_hidden = 0;
    wifi_softap_set_config(&apConfig);

    wifi_set_event_handler_cb(wifi_event_cb);

    espconn_tcp_set_max_con(4);
    espconn_dns_setserver(0, &dnsServerIp);

    os_timer_setfn(&ptimer, timerCb, 0);
    os_timer_arm(&ptimer, 32, 1);

    createTcpServer(80, web_config_client_connected_cb);
    // WDT doesn't reset timers
    lastMasterTick = system_get_time();
    firstTick = system_get_time();
    lastTick = firstTick + 1u;
    returnIdle();
}

