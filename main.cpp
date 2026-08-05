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
#include <vector>

#include "resource.h"
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
        // debug & tst helper
        //std::cout << "PID Update - Error: " << error << " Integral: " << integral << " Derivative: " << derivative << " Output: " << output << std::endl;
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
    //std::cout << "Click!\n";
    INPUT input{0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
    Sleep(20);
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
    

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

std::vector<cv::Point> findAllTargets(const cv::Mat& frame) {
    cv::Mat mask;
    cv::inRange(frame, cv::Scalar(0, 0, 200), cv::Scalar(50, 50, 255), mask);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5,5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Point> centers;
    for (const auto& contour : contours) {
        cv::Moments m = cv::moments(contour);
        if (m.m00 == 0) continue;
        int cx = static_cast<int>(m.m10 / m.m00);
        int cy = static_cast<int>(m.m01 / m.m00);
        // Ignore points too close to the border (optional)
        if (cx < 10 || cy < 10 || cx > frame.cols - 10 || cy > frame.rows - 10)
            continue;
        centers.push_back(cv::Point(cx, cy));
    }
    return centers;
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

// This is data structure passed to EnumWindows and the dialog
struct EnumData {
    std::vector<HWND> handles;
    std::vector<std::wstring> titles;
};

// Callback for EnumWindows 
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    // Skip windows that are hidden or invisible
    if (!IsWindowVisible(hwnd)) {
        return TRUE; 
    }

    // Retrieve the window title length
    int length = GetWindowTextLength(hwnd);
    if (length == 0) {
        return TRUE; 
    }

    // Allocate a buffer and fetch the title text
    std::wstring title(length + 1, L'\0');
    GetWindowTextW(hwnd, &title[0], static_cast<int>(title.size()));

    // Store the HWND in our custom collection passed via lParam
    auto* windowList = reinterpret_cast<EnumData*>(lParam);
    windowList->handles.push_back(hwnd);
    windowList->titles.push_back(title);
    
    // Print out the details
    std::wcout << L"HWND: " << hwnd << L" | Title: " << title.c_str() << std::endl;

    return TRUE; // Return TRUE to keep iterating; FALSE stops the loop
}

// Dialog procedure for the window picker
INT_PTR CALLBACK SelectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static EnumData* pData = nullptr;

    switch (msg) {
    case WM_INITDIALOG: {
        // Store the pointer to the window data
        pData = reinterpret_cast<EnumData*>(lParam);

        HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOWS);
        // Populate the list box with window titles
        for (const auto& title : pData->titles) {
            SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
        }

        // Pre-select the first item if any exist
        if (!pData->titles.empty()) {
            SendMessage(hList, LB_SETCURSEL, 0, 0);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDOK: {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_WINDOWS);
            int idx = static_cast<int>(SendMessage(hList, LB_GETCURSEL, 0, 0));

            if (idx != LB_ERR && pData && idx < static_cast<int>(pData->handles.size())) {
                // Return the selected HWND
                EndDialog(hDlg, reinterpret_cast<INT_PTR>(pData->handles[idx]));
            } else {
                MessageBoxW(hDlg, L"Please select a window from the list.", L"No Selection", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, INT_PTR(0));
            return TRUE;

        case IDC_LIST_WINDOWS:
            if (HIWORD(wParam) == LBN_DBLCLK) {
                // Double-click acts the same as pressing OK
                SendMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// Function to show the window selection dialog and return the selected HWND
HWND SelectWindowGUI() {
    // Enumerate all windows
    EnumData data;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));

    if (data.handles.empty()) {
        MessageBoxW(NULL, L"No visible windows with titles were found.", L"Error", MB_OK | MB_ICONERROR);
        return NULL;
    }

    // Show the modal dialog (blocks until user selects or cancels)
    INT_PTR result = DialogBoxParamW(
        GetModuleHandle(NULL),               // instance
        MAKEINTRESOURCEW(IDD_WINDOW_SELECT),  // dialog resource
        NULL,                                // parent (no owner)
        SelectDlgProc,                       // dialog procedure
        reinterpret_cast<LPARAM>(&data)      // pass the window list
    );

    // Dialog returns the selected HWND, or NULL if cancelled
    return reinterpret_cast<HWND>(result);
}

// Main
int main() {
    /*
    std::vector<HWND> detectedWindows;

    std::cout << "Starting top-level window selection...\n\n";

     Pass the address of the vector to the callback via LPARAM
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&detectedWindows));

    std::cout << "\nSelection finished. Total windows found: " << detectedWindows.size() << std::endl;
    
    system("pause");
    */

    // win
    HWND gameWnd = SelectWindowGUI();

    /*
    //HWND gameWnd = FindWindow(NULL, "Aim");         //replace findwindow
    if (!gameWnd) {
        std::cerr << "Aim window not found! Start Base.exe first." << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return -1;
    }*/
   if (gameWnd == NULL) {
        std::cout << "No window selected. Exiting." << std::endl;
        return 0;
    }
    std::wcout << L"Selected window HWND: " << gameWnd << std::endl;
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
    PID pidx(100, 0, 32.5, 1.0/60.0);
    PID pidy(100, 0, 32.5, 1.0/60.0);
    loadConfig(pidx); // Load config for X-axis PID
    loadConfig(pidy); // Load config for Y-axis PID
    // x,y share same config file for simplicity, but could be separated if desired


    // Flags
    bool autoAimEnabled = false;
    bool pidRunning = true;
    bool autoShoot = true;
    bool debugMode = false;
    bool fixmouse = false;      // fixmouse at center of window

    cv::Point lockedTarget(-1, -1);
    bool hasLock = false;
    int lostCounter = 0;
    const int LOST_THRESHOLD = 15;          // frames before unlocking
    const float DIST_THRESHOLD = 60.0f;     // pixel distance to consider same target

    // Trackbars for Kp, Ki, Kd (range 0..10000, represents 0.0-100.0 with 0.01 steps)
    cv::namedWindow("PID Control", cv::WINDOW_NORMAL);
    cv::resizeWindow("PID Control", 500, 300);
    int kp_slider = static_cast<int>(pidx.kp * 100);
    int ki_slider = static_cast<int>(pidx.ki * 100);
    int kd_slider = static_cast<int>(pidx.kd * 100);
    cv::createTrackbar("Kp (0-100)", "PID Control", &kp_slider, 10000, nullptr);
    cv::createTrackbar("Ki (0-100)", "PID Control", &ki_slider, 10000, nullptr);
    cv::createTrackbar("Kd (0-100)", "PID Control", &kd_slider, 10000, nullptr);

    cv::namedWindow("Auto-Aim Monitor", cv::WINDOW_NORMAL);
    cv::resizeWindow("Auto-Aim Monitor", 640, 480);

    std::cout << "\n=== PID Auto-Aim ===\n";
    std::cout << "Error = target_screen - mouse_screen\n";
    std::cout << "Controls:\n";
    std::cout << "  T - toggle auto-aim\n  S - start PID\n  X - stop PID\n";
    std::cout << "  R - recalculate window rect\n  P - input PID gains\n";
    std::cout << "  C - save config\n  L - load config\n  D - debug output\n  Q - quit\n";
    std::cout << "  O - fix mouse at center\n";

    bool running = true;
    bool lastT=false, lastS=false, lastX=false, lastC=false, lastL=false, lastQ=false, lastP=false, lastD=false, lastR=false, lastO=false;
    int frameCounter = 0;
    double judge = 100;

    
    //std::openfile("judge.txt");
    if (std::ifstream("judge.txt")) {
        std::ifstream infile("judge.txt");
                infile >> judge;
                infile.close();
    } else {
        std::cerr << "Failed to read judge.txt, using default value.\n" << judge << std::endl;
    }

    //loop
    do{
        // Update PID from trackbars (0-10000 represents 0.0-100.0)
        pidx.kp = kp_slider / 100.0;
        pidx.ki = ki_slider / 100.0;
        pidx.kd = kd_slider / 100.0;

        pidy.kp = pidx.kp;
        pidy.ki = pidx.ki; 
        pidy.kd = pidx.kd;

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

        /*
        cv::Point target = findtg(frame);
        bool targetFound = (target.x != -1 && target.y != -1);
        */

        std::vector<cv::Point> candidates = findAllTargets(frame);
        cv::Point currentTarget(-1, -1);
        bool targetFound = false;

        if (!candidates.empty()) {
            if (!hasLock) {
                // *** No lock: pick an initial target
                // ***** pick the one closest to the screen center
                cv::Point center(frame.cols / 2, frame.rows / 2);
                auto it = std::min_element(candidates.begin(), candidates.end(),
                    [&center](const cv::Point& a, const cv::Point& b) {
                        return cv::norm(a - center) < cv::norm(b - center);
                    });
                lockedTarget = *it;
                hasLock = true;
                lostCounter = 0;
                currentTarget = lockedTarget;
                targetFound = true;
            } 
            else {
                // *** Locked: find the candidate closest to lockedTarget 
                auto it = std::min_element(candidates.begin(), candidates.end(),
                    [&](const cv::Point& a, const cv::Point& b) {
                        return cv::norm(a - lockedTarget) < cv::norm(b - lockedTarget);
                    });
                float dist = cv::norm(*it - lockedTarget);
                if (dist < DIST_THRESHOLD) {
                    // Target still visible, update lock position
                    lockedTarget = *it;
                    lostCounter = 0;
                    currentTarget = lockedTarget;
                    targetFound = true;
                } 
                else {
                    // Target lost
                    hasLock = false;
                    lockedTarget = cv::Point(-1, -1);
                    continue;
                    /*lostCounter++;
                    if (lostCounter >= LOST_THRESHOLD) {
                        hasLock = false;
                        lockedTarget = cv::Point(-1, -1);
                        std::cout << "Target lost after " << lostCounter << " out of " << LOST_THRESHOLD << " frames.\n";
                        continue;
                    }*/
                    // currentTarget remains invalid (targetFound = false)
                }
            }
        } else {
            // No target at all
            if (hasLock) {
                hasLock = false;
                lockedTarget = cv::Point(-1, -1);
                continue;
                /*
                lostCounter++;
                if (lostCounter >= LOST_THRESHOLD) {
                    hasLock = false;
                    lockedTarget = cv::Point(-1, -1);
                    std::cout << "Out if :: Target lost after " << lostCounter << " out of " << LOST_THRESHOLD << " frames.\n";
                    continue;
                }*/
            }
        }
        cv::Point target = currentTarget;

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
            
            if (fixmouse) {
                //fix in center
                mousePos.x = 0.5 * (winRect.left + winRect.right);
                mousePos.y = 0.5 * (winRect.top + winRect.bottom);
            }

            double errorX = (mousePos.x - target.x)/judge;
            double errorY = (mousePos.y - target.y)/judge;

            if (debugMode && frameCounter % 60 == 0) {
                std::cout << "Mouse: (" << mousePos.x << "," << mousePos.y << ")  TargetScreen: (" << targetScreenX << "," << targetScreenY << ")" << std::endl;
                std::cout << "target.x = " << target.x << " target.y = " << target.y << std::endl;
                std::cout << "Error: (" << errorX << "," << errorY << ")" << std::endl;
            }

            
            if (std::abs(errorX) < 50000 && std::abs(errorY) < 50000) {
                double moveX = pidx.update(0, errorX);
                double moveY = pidy.update(0, errorY);
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
                    if (debugMode) std::cout << "click\n" << "error.x: "<< errorX << " error.y: " << errorY << "two conditions:" << std::abs(errorX) << " " << std::abs(errorY) << std::endl;
                    Sleep(1);
                } 
            } else {
                if (debugMode) std::cout << "Error too large, skipping movement.\n";
                    pidx.reset();
                    pidy.reset();
            }
        } else {
            pidx.reset();
            pidy.reset();
        }

        // Monitor display
        cv::Mat monitor = frame.clone();
        if (targetFound) {
            cv::circle(monitor, target, 5, cv::Scalar(0, 255, 0), -1);
            cv::circle(monitor, target, 30, cv::Scalar(0, 255, 0), 2);
            cv::putText(monitor, "TARGET FOUND", cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(monitor, "TARGET LOST", cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
        }
        std::string status;
        if (!autoAimEnabled) status = "AUTO-AIM: OFF";
        else if (!pidRunning) status = "PID: STOPPED";
        else status = "PID: RUNNING";
        cv::putText(monitor, status, cv::Point(10, 60),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2,
                    (autoAimEnabled && pidRunning) ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255), 2);
        if (fixmouse) {
            cv::putText(monitor, "FIX MOUSE: ON", cv::Point(10, 100),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 0), 2);
        }
        else {
            cv::putText(monitor, "FIX MOUSE: OFF", cv::Point(10, 100),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 0), 2);
        }
        char pidText[240];
        sprintf(pidText, "X: Kp=%.2f Ki=%.4f Kd=%.2f   Y: Kp=%.2f Ki=%.4f Kd=%.2f",
            pidx.kp, pidx.ki, pidx.kd, pidy.kp, pidy.ki, pidy.kd);
        cv::putText(monitor, pidText, cv::Point(10, 120),
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
        bool nowO = isKeyPressed('O');

        if (nowT && !lastT) autoAimEnabled = !autoAimEnabled;
        if (nowS && !lastS) { pidRunning = true; pidx.reset(); pidy.reset(); }
        if (nowX && !lastX) { pidRunning = false; pidx.reset(); pidy.reset(); }
        if (nowC && !lastC) saveConfig(pidx);       // shared, not need to save twice
        if (nowL && !lastL) {
            // Load config into both controllers (shared config file)
            loadConfig(pidx);
            loadConfig(pidy);
            kp_slider = static_cast<int>(pidx.kp * 100);
            ki_slider = static_cast<int>(pidx.ki * 100);
            kd_slider = static_cast<int>(pidx.kd * 100);
            cv::setTrackbarPos("Kp (0-100)", "PID Control", kp_slider);
            cv::setTrackbarPos("Ki (0-100)", "PID Control", ki_slider);
            cv::setTrackbarPos("Kd (0-100)", "PID Control", kd_slider);
        }
        if (nowP && !lastP) {
            // Input gains for X and copy to Y so both stay consistent
            inputPIDFromConsole(pidx);
            pidy.kp = pidx.kp; pidy.ki = pidx.ki; pidy.kd = pidx.kd;
            kp_slider = static_cast<int>(pidx.kp * 100);
            ki_slider = static_cast<int>(pidx.ki * 100);
            kd_slider = static_cast<int>(pidx.kd * 100);
            cv::setTrackbarPos("Kp (0-100)", "PID Control", kp_slider);
            cv::setTrackbarPos("Ki (0-100)", "PID Control", ki_slider);
            cv::setTrackbarPos("Kd (0-100)", "PID Control", kd_slider);
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
        if (nowO && !lastO) {
            fixmouse = !fixmouse;
            std::cout << "Fix mouse at center: " << (fixmouse ? "ON" : "OFF") << std::endl;
        }
        lastT=nowT; lastS=nowS; lastX=nowX; lastC=nowC; lastL=nowL; lastQ=nowQ; lastP=nowP; lastD=nowD; lastR=nowR, lastO=nowO;

        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        //double targetTime = 1.0 / 60.0;
        //if (elapsed < targetTime) Sleep((targetTime - elapsed) * 1000);
        double dt_actual = elapsed;
        if (dt_actual > 0 && dt_actual < 0.1) { pidx.dt = dt_actual; pidy.dt = dt_actual; }
        frameCounter++;
        //if (debugMode) {
        //    std:: cout << "Frame time: " << elapsed * 1000 << " ms" << std::endl;
        //    std::cout << "Frame: " << frameCounter << std::endl;
        //}
    } while (running);

    cv::destroyAllWindows();
    return 0;
}