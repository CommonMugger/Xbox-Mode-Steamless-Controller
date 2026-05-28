#pragma once
#include <cstdint>

struct StandardGamepadState {
    bool connected = false;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool back = false;
    bool guide = false;
    bool start = false;
    bool leftStick = false;
    bool rightStick = false;
    bool leftShoulder = false;
    bool rightShoulder = false;
    bool dpadUp = false;
    bool dpadDown = false;
    bool dpadLeft = false;
    bool dpadRight = false;
    int16_t leftX = 0;
    int16_t leftY = 0;
    int16_t rightX = 0;
    int16_t rightY = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    bool misc1 = false;
    bool misc2 = false;
    bool misc3 = false;
    bool misc4 = false;
    bool misc5 = false;
    bool misc6 = false;
    bool leftPaddle1 = false;
    bool leftPaddle2 = false;
    bool rightPaddle1 = false;
    bool rightPaddle2 = false;
    bool touchpadButton = false;
    int touchpadCount = 0;
    bool touchpad0Down = false;
    float touchpad0X = 0.0f;
    float touchpad0Y = 0.0f;
    float touchpad0Pressure = 0.0f;
    bool touchpad1Down = false;
    float touchpad1X = 0.0f;
    float touchpad1Y = 0.0f;
    float touchpad1Pressure = 0.0f;
};
