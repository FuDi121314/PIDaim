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
public:
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

};

///////
void moveMouseSmooth(int dx, int dy) {
    INPUT input{ 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}
struct Detection {
    cv::Rect box;
    float confidence;
};

std::vector<Detection> detectObjects(cv::dnn::Net& net, const cv::Mat& frame, float confThreshold, float nmsThreshold) {
    cv::Mat blob = cv::dnn::blobFromImage(frame, 1 / 255.0, cv::Size(416, 416), cv::Scalar(), true, false);
    net.setInput(blob);
    cv::Mat outputs = net.forward();

    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    for (int i = 0; i < outputs.rows; i++) {
        float* data = outputs.ptr<float>(i);
        float confidence = data[4];
        if (confidence > confThreshold) {
            float* classes_scores = data + 5;
            cv::Point classIdPoint;
            double maxClassScore;
            minMaxLoc(cv::Mat(1, outputs.cols - 5, CV_32F, classes_scores), nullptr, &maxClassScore, nullptr, &classIdPoint);
            if (maxClassScore > confThreshold) {
                int centerX = (int)(data[0] * frame.cols);
                int centerY = (int)(data[1] * frame.rows);
                int width = (int)(data[2] * frame.cols);
                int height = (int)(data[3] * frame.rows);
                int left = centerX - width / 2;
                int top = centerY - height / 2;
                boxes.push_back(cv::Rect(left, top, width, height));
                classIds.push_back(classIdPoint.x);
                confidences.push_back(confidence);
            }
        }
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);
    std::vector<Detection> detections;
    for (int idx : indices) {
        detections.push_back({ boxes[idx], confidences[idx] });
    }
    return detections;
}

int main(){
    double dt = 1.0/120.0; // Example time step
    PID pidX(0.025, 0.0001, 0.005, dt);
    PID pidY(0.025, 0.0001, 0.005, dt);
    return 0;
}