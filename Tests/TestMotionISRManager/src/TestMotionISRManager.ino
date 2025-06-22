#include "SparkIntervalTimer.h"

SYSTEM_MODE(AUTOMATIC);
SYSTEM_THREAD(ENABLED);

IntervalTimer myTimer;

class RingBufferPosn
{
public:
    volatile unsigned int _putPos;
    volatile unsigned int _getPos;
    unsigned int _bufLen;

    RingBufferPosn(int maxLen)
    {
        _bufLen = maxLen;
    }

    bool canPut()
    {
        if (_putPos == _getPos)
            return true;
        if (_putPos > _getPos)
            if ((_putPos != _bufLen-1) || (_getPos != 0))
                return true;
        else
            if (_getPos - _putPos > 1)
                return true;
        return false;
    }

    bool canGet()
    {
        return _putPos != _getPos;
    }

    void hasPut()
    {
        _putPos++;
        if (_putPos >= _bufLen)
            _putPos = 0;
    }

    void hasGot()
    {
        _getPos++;
        if (_getPos >= _bufLen)
            _getPos = 0;
    }
};

const int MAX_STEP_GROUPS = 20;

RingBufferPosn ringBufferPosn(MAX_STEP_GROUPS);

class ISRAxisMotionVars
{
public:
    uint32_t _stepUs[MAX_STEP_GROUPS];
    uint32_t _stepNum[MAX_STEP_GROUPS];
    uint32_t _stepDirn[MAX_STEP_GROUPS];
    uint32_t _usAccum;
    uint32_t _stepCount;
    bool _isActive;
    int _stepPin;
    bool _stepPinValue;
    int _dirnPin;
    bool _dirnPinValue;
    // Debug
    bool _dbgLastStepUsValid;
    uint32_t _dbgLastStepUs;
    uint32_t _dbgMaxStepUs;
    uint32_t _dbgMinStepUs;
    volatile long _dbgStepsFromLastZero;

    ISRAxisMotionVars()
    {
        _usAccum = 0;
        _stepCount = 0;
        _isActive = false;
        _stepPin = -1;
        _stepPinValue = false;
        _dirnPin = -1;
        _dirnPinValue = false;
        _dbgStepsFromLastZero = 0;
        dbgResetMinMax();
    }

    void dbgResetMinMax()
    {
        _dbgLastStepUsValid = false;
        _dbgMaxStepUs = 0;
        _dbgMinStepUs = 10000000;
    }

    void dbgResetZero()
    {
        _dbgStepsFromLastZero = 0;
    }

    void setPins(int stepPin, int dirnPin)
    {
        _stepPin = stepPin;
        _dirnPin = dirnPin;
        digitalWrite(_stepPin, false);
        _stepPinValue = false;
        digitalWrite(_dirnPin, false);
        _dirnPinValue = false;
    }
};

static const int ISR_MAX_AXES = 3;
ISRAxisMotionVars axisVars[ISR_MAX_AXES];

void motionManagerISR(void)
{
    static uint32_t lastUs = micros();
    // Get current uS elapsed
    uint32_t curUs = micros();
    uint32_t elapsed = curUs - lastUs;
    if (lastUs > curUs)
        elapsed = 0xffffffff-lastUs+curUs;
    // Check if queue is empty
    if (!ringBufferPosn.canGet())
        return;

    bool allAxesDone = true;
    // Go through axes
    for (int axisIdx = 0; axisIdx < ISR_MAX_AXES; axisIdx++)
    {
        // Get pointer to this axis
        volatile ISRAxisMotionVars* pAxisVars = &(axisVars[axisIdx]);
        if (!pAxisVars->_isActive)
        {
            allAxesDone = false;
            pAxisVars->_usAccum += elapsed;
            uint32_t stepUs = pAxisVars->_stepUs[ringBufferPosn._getPos];
            if (pAxisVars->_usAccum > stepUs)
            {
                if (pAxisVars->_usAccum > stepUs + stepUs)
                    pAxisVars->_usAccum = 0;
                else
                    pAxisVars->_usAccum -= stepUs;
                // Direction
                bool stepDirn = pAxisVars->_stepDirn[ringBufferPosn._getPos];
                if (pAxisVars->_dirnPinValue != stepDirn)
                {
                    pAxisVars->_dirnPinValue = stepDirn;
                    digitalWrite(pAxisVars->_dirnPin, stepDirn);
                }
                // Step
                pAxisVars->_stepPinValue = !pAxisVars->_stepPinValue;
                digitalWrite(pAxisVars->_stepPin, pAxisVars->_stepPinValue);
                pAxisVars->_stepCount++;
                pAxisVars->_dbgStepsFromLastZero += (stepDirn ? 1 : -1);
                if (pAxisVars->_stepCount >= pAxisVars->_stepNum[ringBufferPosn._getPos])
                {
                    pAxisVars->_isActive = true;
                    pAxisVars->_stepCount = 0;
                    pAxisVars->_usAccum = 0;
                    pAxisVars->_dbgLastStepUsValid = false;
                }
                else
                {
                    // Check timing
                    if (pAxisVars->_dbgLastStepUsValid)
                    {
                        uint32_t betweenStepsUs = curUs - pAxisVars->_dbgLastStepUs;
                        if (pAxisVars->_dbgMaxStepUs < betweenStepsUs)
                            pAxisVars->_dbgMaxStepUs = betweenStepsUs;
                        if (pAxisVars->_dbgMinStepUs > betweenStepsUs)
                            pAxisVars->_dbgMinStepUs = betweenStepsUs;
                    }
                    // Record last time
                    pAxisVars->_dbgLastStepUs = curUs;
                    pAxisVars->_dbgLastStepUsValid = true;
                }
            }
        }
    }
    if (allAxesDone)
        ringBufferPosn.hasGot();
    lastUs = curUs;
}

void setup() {
   Serial.begin(115200);
   delay(2000);
   myTimer.begin(motionManagerISR, 10, uSec);
   Serial.println("TestMotionISRManager starting");
   pinMode(A2, OUTPUT);
   digitalWrite(A2, 0);
   pinMode(D2, OUTPUT);
   pinMode(D3, OUTPUT);
   pinMode(D4, OUTPUT);
   pinMode(D5, OUTPUT);
   axisVars[0].setPins(D2, D3);
   axisVars[1].setPins(D4, D5);
   delay(2000);
   digitalWrite(A2, 1);
}

void addSteps(int axisIdx, int stepNum, bool stepDirection, uint32_t uSBetweenSteps)
{
    axisVars[axisIdx]._stepUs[ringBufferPosn._putPos] = uSBetweenSteps;
    axisVars[axisIdx]._stepNum[ringBufferPosn._putPos] = stepNum;
    axisVars[axisIdx]._stepDirn[ringBufferPosn._putPos] = stepDirection;
    axisVars[axisIdx]._usAccum = 0;
    axisVars[axisIdx]._stepCount = 0;
    axisVars[axisIdx]._isActive = (stepNum == 0);
    axisVars[axisIdx]._dbgLastStepUsValid = false;
    axisVars[axisIdx]._dbgMinStepUs = 10000000;
    axisVars[axisIdx]._dbgMaxStepUs = 0;
}

void loop() {
    delay(10000);
    Serial.printlnf("%lu,%lu,%ld  %lu,%lu,%ld  %lu,%lu,%ld  %d %d",
                axisVars[0]._dbgMinStepUs, axisVars[0]._dbgMaxStepUs, axisVars[0]._dbgStepsFromLastZero,
                axisVars[1]._dbgMinStepUs, axisVars[1]._dbgMaxStepUs, axisVars[1]._dbgStepsFromLastZero,
                axisVars[2]._dbgMinStepUs, axisVars[2]._dbgMaxStepUs, axisVars[2]._dbgStepsFromLastZero,
                ringBufferPosn._putPos, ringBufferPosn._getPos);

    for (int i = 0; i < ISR_MAX_AXES; i++)
        axisVars[i].dbgResetMinMax();

    // Add to ring buffer
    if (ringBufferPosn.canPut())
    {
       addSteps(0, 30000, 0, 100);
       addSteps(1, 60000, 0, 50);
    //    addSteps(2, 30, 0, 100000);
       ringBufferPosn.hasPut();
       Serial.printlnf("Put 10000 %d %d", ringBufferPosn._putPos, ringBufferPosn._getPos);
    }
    else
    {
       Serial.println("Can't put");
    }
}
