#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <windows.h>
#include <fstream>
#include <string>
#include <algorithm>

class PID{
    double kp, ki, kd;
    double prev_error=0, integral=0;
    double dt;

    // construct 
    PID(double p, double i, double d, double time_step) : kp(p), ki(i), kd(d), dt(time_step) {}

    //graph
    double update(double setpoint, double measurement) {
        double error = setpoint - measurement;
        integral += error * dt;
        double derivative = (error - prev_error) / dt;
        double output = kp * error + ki * integral + kd * derivative;
        prev_error = error;
        return output;
    }

    //reset
    void reset() {
        prev_error = 0;
        integral = 0;
    }

}

///////
void moveMouseSmooth(int dx, int dy) {
    INPUT input{ 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

int main(){
    double dt = 1.0/120.0; // Example time step
    PID pidX(0.025, 0.0001, 0.005, dt);
    PID pidY(0.025, 0.0001, 0.005, dt);
    return 0;
}