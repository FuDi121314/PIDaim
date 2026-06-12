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
#include <sstream>
#include <conio.h>   

bool g_running = true;

class PID {
public:
    double kp, ki, kd;
    double prev_error = 0;
    double integral = 0;
    double dt;

    PID(double p, double i, double d, double time_step)
        : kp(p), ki(i), kd(d), dt(time_step) {}

    double update(double setpoint, double measurement) {
        double error = setpoint - measurement;
        integral += error * dt;
        const double INTEGRAL_LIMIT = 10000.0;
        if (integral > INTEGRAL_LIMIT) integral = INTEGRAL_LIMIT;
        if (integral < -INTEGRAL_LIMIT) integral = -INTEGRAL_LIMIT;
        double derivative = (error - prev_error) / dt;
        double output = kp * error + ki * integral + kd * derivative;
        prev_error = error;
        return output;
    }

    void reset() {
        prev_error = 0;
        integral = 0;
    }
};


// Mouse control
void moveMouseRelative(int dx, int dy) {
    INPUT input{0};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void mouseClick() {
    std::cout << "Click!\n";
    /*
    INPUT input{0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
    Sleep(20);
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
    */

}

// Screen capture (BitBlt)
cv::Mat captureWindow(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT pt = {0, 0};
    ClientToScreen(hwnd, &pt);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0) {
        GetWindowRect(hwnd, &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        pt.x = rect.left;
        pt.y = rect.top;
    }

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

// Find target
cv::Point findtg(const cv::Mat& frame) {
    cv::Mat mask;
    cv::inRange(frame, cv::Scalar(0, 0, 200), cv::Scalar(50, 50, 255), mask);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5,5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return cv::Point(-1, -1);

    auto largest = std::max_element(contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    cv::Moments m = cv::moments(*largest);
    if (m.m00 == 0) return cv::Point(-1, -1);
    int cx = static_cast<int>(m.m10 / m.m00);
    int cy = static_cast<int>(m.m01 / m.m00);
    if (cx < 10 || cy < 10 || cx > frame.cols - 10 || cy > frame.rows - 10)
        return cv::Point(-1, -1);
    return cv::Point(cx, cy);
}

// Save/Load config
void saveConfig(const PID& pid, const std::string& filename = "pid_config.txt") {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << pid.kp << " " << pid.ki << " " << pid.kd;
        file.close();
        std::cout << "Config saved (Kp=" << pid.kp << ", Ki=" << pid.ki << ", Kd=" << pid.kd << ")" << std::endl;
    } else {
        std::cerr << "Failed to save config!" << std::endl;
    }
}

void loadConfig(PID& pid, const std::string& filename = "pid_config.txt") {
    std::ifstream file(filename);
    if (file.is_open()) {
        file >> pid.kp >> pid.ki >> pid.kd;
        file.close();
        std::cout << "Loaded PID: Kp=" << pid.kp << " Ki=" << pid.ki << " Kd=" << pid.kd << std::endl;
    } else {
        std::cout << "No saved config, using defaults." << std::endl;
    }
}

// Console input for exact double numbers
void inputPIDFromConsole(PID& pid) {
    std::cout << "Enter new PID gains (Kp Ki Kd) separated by spaces: ";
    double kp, ki, kd;
    std::cin >> kp >> ki >> kd;
    if (std::cin.fail()) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid input. Gains unchanged." << std::endl;
    } else {
        pid.kp = kp;
        pid.ki = ki;
        pid.kd = kd;
        std::cout << "New gains: Kp=" << pid.kp << " Ki=" << pid.ki << " Kd=" << pid.kd << std::endl;
    }
}

// Real-time key state check
bool isKeyPressed(int vkey) {
    return (GetAsyncKeyState(vkey) & 0x8000) != 0;
}

// Main
int main() {

    // win
    HWND gameWnd = FindWindow(NULL, "Aim");
    if (!gameWnd) {
        std::cerr << "Aim window not found! Start Base.exe first." << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return -1;
    }
    SetForegroundWindow(gameWnd);
    Sleep(500);

    // Get initial window rect for center (used only for display)
    RECT rect;
    GetClientRect(gameWnd, &rect);
    int centerX = (rect.right - rect.left) / 2;
    int centerY = (rect.bottom - rect.top) / 2;

    // Move mouse to center of game window (good starting point)
    POINT pt = {0,0};
    ClientToScreen(gameWnd, &pt);
    SetCursorPos(pt.x + centerX, pt.y + centerY);
    Sleep(300);

    // PID init
    PID pid(100, 0, 32.5, 1.0/60.0);
    loadConfig(pid);

    // Flags
    bool autoAimEnabled = true;
    bool pidRunning = true;
    bool autoShoot = true;
    bool debugMode = false;

    // Trackbars for Kp, Ki, Kd (range 0..10000 with decimal precision)
    cv::namedWindow("PID Control", cv::WINDOW_NORMAL);
    cv::resizeWindow("PID Control", 500, 300);
    int kp_slider = static_cast<int>(pid.kp * 100);
    int ki_slider = static_cast<int>(pid.ki * 100);
    int kd_slider = static_cast<int>(pid.kd * 100);
    cv::createTrackbar("Kp (x100)", "PID Control", &kp_slider, 100000, nullptr);
    cv::createTrackbar("Ki (x100)", "PID Control", &ki_slider, 100000, nullptr);
    cv::createTrackbar("Kd (x100)", "PID Control", &kd_slider, 100000, nullptr);

    cv::namedWindow("Auto-Aim Monitor", cv::WINDOW_NORMAL);
    cv::resizeWindow("Auto-Aim Monitor", 640, 480);

    std::cout << "\n=== PID Auto-Aim ===\n";
    std::cout << "Error = target_screen - mouse_screen\n";
    std::cout << "Controls:\n";
    std::cout << "  T - toggle auto-aim\n  S - start PID\n  X - stop PID\n";
    std::cout << "  R - recalculate window rect\n  P - input PID gains\n";
    std::cout << "  C - save config\n  L - load config\n  D - debug output\n  Q - quit\n";

    bool running = true;
    bool lastT=false, lastS=false, lastX=false, lastC=false, lastL=false, lastQ=false, lastP=false, lastD=false, lastR=false;
    int frameCounter = 0;

    while (running) {
        // Update PID from trackbars
        pid.kp = kp_slider / 100.0;
        pid.ki = ki_slider / 100.0;
        pid.kd = kd_slider / 100.0;

        auto start = std::chrono::steady_clock::now();

        // Get current window rectangle (in case of resize/fullscreen)
        GetClientRect(gameWnd, &rect);
        int windowWidth = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;
        if (windowWidth > 0 && windowHeight > 0) {
            centerX = windowWidth / 2;
            centerY = windowHeight / 2;
        }

        cv::Mat frame = captureWindow(gameWnd);
        if (frame.empty()) break;

        cv::Point target = findtg(frame);
        bool targetFound = (target.x != -1 && target.y != -1);

        // Debug output
        if (debugMode && frameCounter % 60 == 0) {
            std::cout << "Target (window): (" << target.x << "," << target.y << ")  Center: (" << centerX << "," << centerY << ")" << std::endl;
        }

        
        if (autoAimEnabled && pidRunning && targetFound) {
            // Get current mouse cursor position (screen)
            POINT mousePos;
            GetCursorPos(&mousePos);

            // Convert target (window-relative) to screen coordinates
            RECT winRect;
            GetWindowRect(gameWnd, &winRect);
            int targetScreenX = winRect.left + target.x;
            int targetScreenY = winRect.top + target.y;
            double judge = 100;

            //std::openfile("judge.txt");
            if (std::ifstream("judge.txt")) {
                std::ifstream infile("judge.txt");
                infile >> judge;
                infile.close();
            } else {
                std::cerr << "Failed to read judge.txt, using default value.\n" << judge << std::endl;
            }

            double errorX = (mousePos.x - target.x)/judge;
            double errorY = (mousePos.y - target.y)/judge;

            if (debugMode && frameCounter % 60 == 0) {
                std::cout << "Mouse: (" << mousePos.x << "," << mousePos.y << ")  TargetScreen: (" << targetScreenX << "," << targetScreenY << ")" << std::endl;
                std::cout << "target.x = " << target.x << " target.y = " << target.y << std::endl;
                std::cout << "Error: (" << errorX << "," << errorY << ")" << std::endl;
            }

            
            if (std::abs(errorX) < 50000 && std::abs(errorY) < 50000) {
                double moveX = pid.update(0, errorX);
                double moveY = pid.update(0, errorY);
                //const int MAX_MOVE = 25;
                //if (moveX > MAX_MOVE) moveX = MAX_MOVE;
                //if (moveX < -MAX_MOVE) moveX = -MAX_MOVE;
                //if (moveY > MAX_MOVE) moveY = MAX_MOVE;
                //if (moveY < -MAX_MOVE) moveY = -MAX_MOVE;
                moveMouseRelative((int)moveX, (int)moveY);
                if (debugMode && frameCounter % 60 == 0) {
                    std::cout << "move.x: "<<moveX << std::endl;
                    std::cout << "move.y: "<<moveY << std::endl;
                }
                if (autoShoot && std::abs(errorX) < 10 && std::abs(errorY) < 10) {
                    mouseClick();
                    Sleep(1);
                } 
            } else {
                if (debugMode) std::cout << "Error too large, skipping movement.\n";
                pid.reset();
            }
        } else {
            pid.reset();
        }

        // Monitor display
        cv::Mat monitor = frame.clone();
        if (targetFound) {
            cv::circle(monitor, target, 5, cv::Scalar(0, 255, 0), -1);
            cv::circle(monitor, target, 30, cv::Scalar(0, 255, 0), 2);
            cv::putText(monitor, "TARGET FOUND", cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(monitor, "TARGET LOST", cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        }
        std::string status;
        if (!autoAimEnabled) status = "AUTO-AIM: OFF";
        else if (!pidRunning) status = "PID: STOPPED";
        else status = "PID: RUNNING";
        cv::putText(monitor, status, cv::Point(10, 60),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    (autoAimEnabled && pidRunning) ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255), 2);
        char pidText[120];
        sprintf(pidText, "Kp = %.2f   Ki = %.4f   Kd = %.2f", pid.kp, pid.ki, pid.kd);
        cv::putText(monitor, pidText, cv::Point(10, 90),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);
        cv::imshow("Auto-Aim Monitor", monitor);
        cv::waitKey(1);

        // Keyboard handling
        bool nowT = isKeyPressed('T');
        bool nowS = isKeyPressed('S');
        bool nowX = isKeyPressed('X');
        bool nowC = isKeyPressed('C');
        bool nowL = isKeyPressed('L');
        bool nowQ = isKeyPressed('Q');
        bool nowP = isKeyPressed('P');
        bool nowD = isKeyPressed('D');
        bool nowR = isKeyPressed('R');

        if (nowT && !lastT) autoAimEnabled = !autoAimEnabled;
        if (nowS && !lastS) { pidRunning = true; pid.reset(); }
        if (nowX && !lastX) { pidRunning = false; pid.reset(); }
        if (nowC && !lastC) saveConfig(pid);
        if (nowL && !lastL) {
            loadConfig(pid);
            kp_slider = static_cast<int>(pid.kp * 100);
            ki_slider = static_cast<int>(pid.ki * 100);
            kd_slider = static_cast<int>(pid.kd * 100);
            cv::setTrackbarPos("Kp (x100)", "PID Control", kp_slider);
            cv::setTrackbarPos("Ki (x100)", "PID Control", ki_slider);
            cv::setTrackbarPos("Kd (x100)", "PID Control", kd_slider);
        }
        if (nowP && !lastP) {
            inputPIDFromConsole(pid);
            kp_slider = static_cast<int>(pid.kp * 100);
            ki_slider = static_cast<int>(pid.ki * 100);
            kd_slider = static_cast<int>(pid.kd * 100);
            cv::setTrackbarPos("Kp (x100)", "PID Control", kp_slider);
            cv::setTrackbarPos("Ki (x100)", "PID Control", ki_slider);
            cv::setTrackbarPos("Kd (x100)", "PID Control", kd_slider);
        }
        if (nowD && !lastD) {
            debugMode = !debugMode;
            std::cout << "Debug mode " << (debugMode ? "ON" : "OFF") << std::endl;
        }
        if (nowR && !lastR) {
            GetClientRect(gameWnd, &rect);
            centerX = (rect.right - rect.left) / 2;
            centerY = (rect.bottom - rect.top) / 2;
            std::cout << "Recenter: new window center (" << centerX << "," << centerY << ")" << std::endl;
        }
        if (nowQ && !lastQ) running = false;

        lastT=nowT; lastS=nowS; lastX=nowX; lastC=nowC; lastL=nowL; lastQ=nowQ; lastP=nowP; lastD=nowD; lastR=nowR;

        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        double targetTime = 1.0 / 60.0;
        if (elapsed < targetTime) Sleep((targetTime - elapsed) * 1000);
        double dt_actual = elapsed;
        if (dt_actual > 0 && dt_actual < 0.1) pid.dt = dt_actual;
        frameCounter++;
    }

    cv::destroyAllWindows();
    return 0;
}