#include <opencv2/opencv.hpp>
#include <iostream>
#include <random>
#include <windows.h>

int score = 0;
bool running = true;

struct Target {
    int x, y;
    int radius = 30;
} target;

void onMouse(int event, int x, int y, int flags, void*) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        int dx = x - target.x;
        int dy = y - target.y;
        if (dx*dx + dy*dy < target.radius * target.radius) {
            score++;
            std::cout << "Score: " << score << std::endl;
            // Move target to random position
            target.x = rand() % (800 - 2*target.radius) + target.radius;
            target.y = rand() % (600 - 2*target.radius) + target.radius;
        }
    }
}

int main() {
    srand((unsigned)time(NULL));
    cv::namedWindow("Aim", cv::WINDOW_NORMAL);
    cv::resizeWindow("Aim", 800, 600);

    
    target.x = rand() % (800 - 2*target.radius) + target.radius;
    target.y = rand() % (600 - 2*target.radius) + target.radius;
    cv::setMouseCallback("Aim", onMouse, NULL);

    std::cout << "=== Shooting Game ===" << std::endl;
    std::cout << "Click the red circle. Score: " << score << std::endl;
    std::cout << "Press ESC to quit." << std::endl;

    while (running) {
        cv::Mat frame(600, 800, CV_8UC3, cv::Scalar(50, 50, 50));
        cv::circle(frame, cv::Point(target.x, target.y), target.radius, cv::Scalar(0, 0, 255), -1);
        cv::circle(frame, cv::Point(target.x, target.y), target.radius, cv::Scalar(255, 255, 255), 2);
        cv::putText(frame, "Score: " + std::to_string(score), cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
        cv::imshow("Aim", frame);
        if (cv::waitKey(10) == 27) running = false;
    }
    cv::destroyAllWindows();
    return 0;
}