// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "util.hh"
#include "config.hh"
#include "drive.hh"

// Drive speed
static double drv_x;
static double drv_y;
static double drv_w;

// Motor control
static double dc_angle = 0.0;
static int dc_ticks[4] = {};
static int dc_speed[4] = {};

// PID state
static double pid_acc[4] = {};
static double pid_prev[4] = {};

static inline void IncrementEncoderSpeed(int idx, int dir_pin)
{
    dc_ticks[idx] += digitalRead(dir_pin) ? 1 : -1;
}

void InitDrive()
{
    // Encoder speed
    attachInterrupt(digitalPinToInterrupt(PIN_ENC0_INT), []() { IncrementEncoderSpeed(0, PIN_ENC0_DIR); }, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC1_INT), []() { IncrementEncoderSpeed(0, PIN_ENC1_DIR); }, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC2_INT), []() { IncrementEncoderSpeed(0, PIN_ENC2_DIR); }, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC3_INT), []() { IncrementEncoderSpeed(0, PIN_ENC3_DIR); }, FALLING);

    // DC driver direction pins
    pinMode(PIN_DC0_DIR, OUTPUT);
    pinMode(PIN_DC1_DIR, OUTPUT);
    pinMode(PIN_DC2_DIR, OUTPUT);
    pinMode(PIN_DC3_DIR, OUTPUT);

    // DC driver PWM pins
    pinMode(PIN_DC0_PWM, OUTPUT);
    pinMode(PIN_DC1_PWM, OUTPUT);
    pinMode(PIN_DC2_PWM, OUTPUT);
    pinMode(PIN_DC3_PWM, OUTPUT);
}

static void WriteMotorSpeed(int dir_pin, int pwm_pin, int speed)
{
    if (speed >= 0) {
        digitalWrite(dir_pin, 0);
        analogWrite(pwm_pin, speed);
    } else {
        digitalWrite(dir_pin, 1);
        analogWrite(pwm_pin, -speed);
    }
}

void ProcessDrive()
{
    PROCESS_EVERY(5000);

    // Forward kinematics matrix:
    // -sin((45 + 90)°)  | cos((45 + 90)°)  | 1
    // -sin((135 + 90)°) | cos((135 + 90)°) | 1
    // -sin((225 + 90)°) | cos((225 + 90)°) | 1
    // -sin((315 + 90)°) | cos((315 + 90)°) | 1
    //
    // Inverse kinematics matrix:
    // -1/sqrt(2) | -1/sqrt(2) | 1
    //  1/sqrt(2) | -1/sqrt(2) | 1
    //  1/sqrt(2) |  1/sqrt(2) | 1
    // -1/sqrt(2) |  1/sqrt(2) | 1

    // DC speed constants
    static const double kl = 1.0;
    static const double kw = 1.0;

    // PID constants
    static const double kp = 1.0;
    static const double ki = 0.0;
    static const double kd = 0.0;

    int ticks[4];
    noInterrupts();
    memcpy(ticks, dc_ticks, sizeof(dc_ticks));
    memset(dc_ticks, 0, sizeof(dc_ticks));
    interrupts();

    // Eventually we will integrate gyroscope information (Kalman filter)
    dc_angle += (double)ticks[0] / kw + (double)ticks[1] / kw +
                (double)ticks[2] / kw + (double)ticks[3] / kw;

    // World coordinates to robot coordinates
    double self_x = drv_x * cosf(-dc_angle) - drv_y * sinf(-dc_angle);
    double self_y = drv_x * sinf(-dc_angle) + drv_y * cosf(-dc_angle);
    double self_w = drv_w;

    // Compute target speed for all 4 motors
    int target[4] = {};
    {
        double x = self_x * kl;
        double y = self_y * kl;
        double w = self_w * kw;

        target[0] = (int)(x * -0.7071 + y * -0.7071 + w * 1.0);
        target[1] = (int)(x *  0.7071 + y * -0.7071 + w * 1.0);
        target[2] = (int)(x *  0.7071 + y *  0.7071 + w * 1.0);
        target[3] = (int)(x * -0.7071 + y *  0.7071 + w * 1.0);
    }

    // Run target DC speeds through PID controller
    for (int i = 0; i < 4; i++) {
        double error = (double)(target[i] - ticks[i]);
        double delta = error - pid_prev[i];

        ticks[i] = 0;
        pid_acc[i] += error;
        pid_prev[i] = error;

        dc_speed[i] += (int)(kp * error + ki * pid_acc[i] + kd * delta);
        dc_speed[i] = constrain(dc_speed[i], -255, 255);
    }

    WriteMotorSpeed(PIN_DC0_DIR, PIN_DC0_PWM, dc_speed[0]);
    WriteMotorSpeed(PIN_DC1_DIR, PIN_DC1_PWM, dc_speed[1]);
    WriteMotorSpeed(PIN_DC2_DIR, PIN_DC2_PWM, dc_speed[2]);
    WriteMotorSpeed(PIN_DC3_DIR, PIN_DC3_PWM, dc_speed[3]);
}

void SetDriveSpeed(double x, double y, double w)
{
    drv_x = x;
    drv_y = y;
    drv_w = fmodf(-w, 360.0) * PI / 180.0;
}
