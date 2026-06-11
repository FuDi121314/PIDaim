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

bool g_running = true;

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
void moveMouseRelative(int dx, int dy) {
    INPUT input{0};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void mouseClick() {
    INPUT input{0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
    Sleep(20);
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}


struct Detection {
    cv::Rect box;
    float confidence;
};


cv::Point findRedCircle(const cv::Mat& frame) {
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

    // Red color has two ranges in HSV (wraps around 0)
    cv::Mat mask1, mask2;
    cv::inRange(hsv, cv::Scalar(0, 70, 50), cv::Scalar(10, 255, 255), mask1);
    cv::inRange(hsv, cv::Scalar(170, 70, 50), cv::Scalar(180, 255, 255), mask2);
    cv::Mat mask = mask1 | mask2;

    // Morphological cleanup
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5,5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) return cv::Point(-1, -1);

    // Find largest contour (the circle)
    auto largest = std::max_element(contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });

    cv::Moments m = cv::moments(*largest);
    if (m.m00 == 0) return cv::Point(-1, -1);
    int cx = static_cast<int>(m.m10 / m.m00);
    int cy = static_cast<int>(m.m01 / m.m00);
    return cv::Point(cx, cy);
}


cv::Mat captureWindow(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT pt = {0, 0};
    ClientToScreen(hwnd, &pt);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, pt.x, pt.y, SRCCOPY);

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    cv::Mat frame(height, width, CV_8UC4);
    GetDIBits(hdcMem, hBitmap, 0, height, frame.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    cv::Mat bgr;
    cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return bgr;
}


int main(){

 HWND gameWnd = FindWindow(NULL, "Shooting Game");
    if (!gameWnd) {
        std::cerr << "Shooting Game window not found! Start the game first." << std::endl;
        std::cerr << "Press any key to exit..." << std::endl;
        std::cin.get();
        return -1;
    }
    std::cout << "Found Shooting Game window!" << std::endl;

    // Bring window to foreground
    SetForegroundWindow(gameWnd);
    Sleep(500);

    // Get window size
    RECT rect;
    GetClientRect(gameWnd, &rect);
    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;
    int centerX = windowWidth / 2;
    int centerY = windowHeight / 2;

    //  PID setup
    double dt = 1.0 / 60.0;
    PID pidX(0.025, 0.0001, 0.005, dt);
    PID pidY(0.025, 0.0001, 0.005, dt);
    bool autoAimActive = true;
    bool autoShoot = true;

    std::cout << "\n=== Auto-Aim on Shooting Game ===" << std::endl;
    std::cout << "Auto-aim is ON (press 'Q' in preview window to quit)" << std::endl;
    std::cout << "The program will move your mouse toward the red circle." << std::endl;

    cv::namedWindow("Auto-Aim Preview", cv::WINDOW_NORMAL);
    cv::resizeWindow("Auto-Aim Preview", 640, 480);

    while (g_running) {
        auto start = std::chrono::steady_clock::now();

        // Capture game window
        cv::Mat frame = captureWindow(gameWnd);
        if (frame.empty()) break;

        // Find red circle
        cv::Point target = findRedCircle(frame);
        bool targetFound = (target.x != -1);

        // Draw detection on preview
        if (targetFound) {
            cv::circle(frame, target, 5, cv::Scalar(0, 255, 0), -1);
            cv::circle(frame, target, 30, cv::Scalar(0, 255, 0), 2);
        }

        // Auto-aim logic
        if (autoAimActive && targetFound) {
            double errorX = target.x - centerX;
            double errorY = target.y - centerY;

            double moveX = pidX.update(0, errorX);
            double moveY = pidY.update(0, errorY);

            const int MAX_MOVE = 25;
            if (moveX > MAX_MOVE) moveX = MAX_MOVE;
            if (moveX < -MAX_MOVE) moveX = -MAX_MOVE;
            if (moveY > MAX_MOVE) moveY = MAX_MOVE;
            if (moveY < -MAX_MOVE) moveY = -MAX_MOVE;

            moveMouseRelative((int)moveX, (int)moveY);

            // Auto-shoot when close enough
            if (autoShoot && std::abs(errorX) < 15 && std::abs(errorY) < 15) {
                mouseClick();
                Sleep(150);  // debounce
            }
        } else {
            pidX.reset();
            pidY.reset();
        }

        // Show preview
        cv::putText(frame, "Auto-Aim: ACTIVE", cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, "Target: " + std::string(targetFound ? "Found" : "Lost"), cv::Point(10, 60),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, targetFound ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255), 2);
        cv::imshow("Auto-Aim Preview", frame);

        int key = cv::waitKey(1);
        if (key == 'q' || key == 27) g_running = false;

        // Maintain ~30 FPS
        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        double targetTime = 1.0 / 30.0;
        if (elapsed < targetTime) {
            Sleep((targetTime - elapsed) * 1000);
        }
        double dt_actual = elapsed;
        if (dt_actual > 0 && dt_actual < 0.1) {
            pidX.dt = dt_actual;
            pidY.dt = dt_actual;
        }
    }

    cv::destroyAllWindows();
    return 0;
}