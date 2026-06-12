#include <opencv2/opencv.hpp>
#include <iostream>
#include <random>
#include <windows.h>


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

// ------------------------------
// Mouse control
// ------------------------------
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


struct Target {
    int x, y;
    int radius = 30;
};

int score = 0;
bool autoAimEnabled = false;
bool running = true;

// ------------------------------
// Mouse callback for manual shooting
// ------------------------------
void onMouse(int event, int x, int y, int flags, void* userdata) {
    if (autoAimEnabled) return; // auto‑aim handles shooting
    if (event == cv::EVENT_LBUTTONDOWN) {
        Target* target = (Target*)userdata;
        int dx = x - target->x;
        int dy = y - target->y;
        if (dx*dx + dy*dy < target->radius * target->radius) {
            score++;
            std::cout << "Hit! Score: " << score << std::endl;
            // Move target to random position
            target->x = rand() % (800 - 2*target->radius) + target->radius;
            target->y = rand() % (600 - 2*target->radius) + target->radius;
        }
    }
}

// ------------------------------
// Auto‑aim loop (runs in separate thread)
// ------------------------------
void autoAimThread(Target* target, bool* enabled, bool* running) {
    PID pidX(0.03, 0.0001, 0.005, 1.0/60.0);
    PID pidY(0.03, 0.0001, 0.005, 1.0/60.0);
    
    const int screenCenterX = 400;   // assuming game window is 800x600 at top-left
    const int screenCenterY = 300;   // adjust if window position differs
    bool lastEnabled = false;
    
    while (*running) {
        if (*enabled) {
            // Map target position (relative to game window) to absolute screen coordinates
            // Get game window position (assuming it's the active OpenCV window)
            HWND gameWnd = FindWindow(NULL, "Shooting Game");
            if (gameWnd) {
                RECT rect;
                GetWindowRect(gameWnd, &rect);
                int targetScreenX = rect.left + target->x;
                int targetScreenY = rect.top + target->y;
                
                double errorX = targetScreenX - (rect.left + screenCenterX);
                double errorY = targetScreenY - (rect.top + screenCenterY);
                
                double moveX = pidX.update(0, errorX);
                double moveY = pidY.update(0, errorY);
                
                // Clamp movement
                const int MAX_MOVE = 20;
                if (moveX > MAX_MOVE) moveX = MAX_MOVE;
                if (moveX < -MAX_MOVE) moveX = -MAX_MOVE;
                if (moveY > MAX_MOVE) moveY = MAX_MOVE;
                if (moveY < -MAX_MOVE) moveY = -MAX_MOVE;
                
                moveMouseRelative((int)moveX, (int)moveY);
                
                // If crosshair is close enough, shoot
                if (std::abs(errorX) < 15 && std::abs(errorY) < 15) {
                    mouseClick();
                    // Target will be moved by the main thread after hit detection
                    Sleep(200); // debounce
                }
            }
        } else {
            pidX.reset();
            pidY.reset();
        }
        Sleep(16); // ~60 Hz
    }
}



int main() {
    srand((unsigned)time(NULL));
    
    cv::namedWindow("Shooting Game", cv::WINDOW_NORMAL);
    cv::resizeWindow("Shooting Game", 800, 600);
    
    Target target;
    target.radius = 30;
    target.x = rand() % (800 - 2*target.radius) + target.radius;
    target.y = rand() % (600 - 2*target.radius) + target.radius;
    
    cv::setMouseCallback("Shooting Game", onMouse, &target);
    
    // Start auto‑aim thread
    std::thread aimThread(autoAimThread, &target, &autoAimEnabled, &running);
    
    std::cout << "=== Shooting Game ===" << std::endl;
    std::cout << "Click on the red circle to shoot (manual mode)." << std::endl;
    std::cout << "Press 'A' to toggle auto‑aim (PID controller)." << std::endl;
    std::cout << "Press 'ESC' to quit." << std::endl;
    
    while (running) {
        cv::Mat frame(600, 800, CV_8UC3, cv::Scalar(50, 50, 50));
        
        // Draw target
        cv::circle(frame, cv::Point(target.x, target.y), target.radius, cv::Scalar(0, 0, 255), -1);
        cv::circle(frame, cv::Point(target.x, target.y), target.radius, cv::Scalar(255, 255, 255), 2);
        
        // Draw score and mode
        cv::putText(frame, "Score: " + std::to_string(score), cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
        std::string modeText = autoAimEnabled ? "AUTO-AIM ON (PID)" : "MANUAL MODE";
        cv::putText(frame, modeText, cv::Point(10, 70),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, autoAimEnabled ? cv::Scalar(0, 255, 0) : cv::Scalar(200, 200, 0), 2);
        
        cv::imshow("Shooting Game", frame);
        
        int key = cv::waitKey(10);
        if (key == 27) { // ESC
            running = false;
        } else if (key == 'a' || key == 'A') {
            autoAimEnabled = !autoAimEnabled;
            std::cout << "Auto‑aim " << (autoAimEnabled ? "ENABLED" : "DISABLED") << std::endl;
        }
        
        // Manual shooting already handled by mouse callback.
        // Auto‑aim thread will click when close enough, but we need to update score when auto‑aim hits.
        // To avoid race, we handle hit detection in main loop for both manual and auto‑aim clicks.
        // However, auto‑aim thread calls mouseClick() which generates a left button down/up.
        // OpenCV's mouse callback will catch that and update score. So no extra work needed.
    }
    
    aimThread.join();
    cv::destroyAllWindows();
    return 0;
}