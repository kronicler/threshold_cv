//
//  main.cpp
//  k_nearest_detector_v2
//
//  Created by Ler Wilson on 11/9/18.
//  Copyright © 2018 Ler Wilson. All rights reserved.
//

#include <iostream>
#include <stdio.h>
#include <stdarg.h>
#include "opencv2/imgproc.hpp"
#include "opencv2/core.hpp"
#include "opencv2/highgui.hpp"
#include <image_transport/image_transport.h>
#include <sensor_msgs/Image.h>
#include <cv_bridge/cv_bridge.h>
#include "ros/ros.h"
#include <dynamic_reconfigure/server.h>
#include <k_nearest/k_nearestConfig.h>

#define INT_INF 2147483640

using namespace cv;
using namespace std;
using namespace ros;

float MULTIPLER_GLOBAL = 10;
float MIN_GLOBAL, MAX_GLOBAL;
image_transport::Publisher image_pub;

string COLOR_SELECT = "PURE_GREEN";
bool DISTANCE_DIFFERENCE_MANUAL_BOOL = false;
int DISTANCE_DIFFERENCE_MANUAL;
bool DISTANCE_LIMIT_FILTER_MANUAL_BOOL = false;
float DISTANCE_LIMIT_FILTER_MANUAL;

bool MANUAL_COLORS_BOOL = false;
float MANUAL_L = 0;
float MANUAL_A = 0;
float MANUAL_B = 0;

float CLIMB = 0.1;

class ColorMap {
    vector<float> colorArray = {0,0,0};
    int DISTANCE_DIFFERENCE = 1000;
    float DISTANCE_LIMIT_FILTER = 255; // More means larger allowance
    float MIN_HARDSTOP = 50; // More means more leeway. Use this to stop detection if color you want is really not in view

public:
    ColorMap (string COLOR_SELECT) {
        if (COLOR_SELECT == "PURE_RED") {
            colorArray = {54.29/100 * 255, 80.81 + 127, 69.89 + 127};
            DISTANCE_DIFFERENCE = 800;
        }
        else if (COLOR_SELECT == "PURE_GREEN") {
            colorArray = {46.228/100 * 255, -51.699 + 127,  49.897 + 127};
            DISTANCE_DIFFERENCE = 500;
        }
        else if (COLOR_SELECT == "PURE_BLUE") {
            colorArray = {29.57/100 * 255, 68.30 + 127,  -112.03 + 127};
        }
        else if (COLOR_SELECT == "PURE_YELLOW") {
            colorArray = {97.139/100 * 255, -21.558 + 127,  94.477 + 127};
        }
        else if (COLOR_SELECT == "DARK_GREEN") {
            colorArray = {36.202/100 * 255, -43.37 + 127,  41.858 + 127};
        }
        else if (COLOR_SELECT == "WEIRD_GREEN") {
            colorArray = {116.09264337851928,  115.00685610010427, 127.26386339937434};
            DISTANCE_DIFFERENCE = 3000;
        }
        else if (COLOR_SELECT == "WEIRD_RED") {
            colorArray = {26.02/100 * 255,  21.96 + 127, 16.98 + 127};
        }
        else if (COLOR_SELECT == "WEIRD_BLUE") {
            DISTANCE_DIFFERENCE = 2000;
            colorArray = {125.50277161862527, 135.06203806356245, 106.72828898743533};
        }
        else if (COLOR_SELECT == "WEIRD_YELLOW") {
            colorArray = {112.36884505868463, 134.8206993541218, 154.5124661434822};
        }
        else if (COLOR_SELECT == "BRIGHTER_BLUE") {
            colorArray = {141.05574433656957, 142.39014563106795, 85.23480582524272};
        }
        else {
            colorArray = {97.139/100 * 255, -21.558 + 127,  94.477 + 127};
        }

        if (DISTANCE_DIFFERENCE_MANUAL_BOOL) {
            DISTANCE_DIFFERENCE = DISTANCE_DIFFERENCE_MANUAL;
        }

        if (DISTANCE_LIMIT_FILTER_MANUAL_BOOL) {
            DISTANCE_LIMIT_FILTER = DISTANCE_LIMIT_FILTER_MANUAL;
        }

        if (MANUAL_COLORS_BOOL) {
            colorArray = {MANUAL_L, MANUAL_A, MANUAL_B};
        }
    }
    
    vector<float> getColor () {
        return colorArray;
    }
    int getDistanceDifference () {
        return DISTANCE_DIFFERENCE;
    }
    float getDistanceLimitFilter () {
        return DISTANCE_LIMIT_FILTER;
    }
    float getMinHardStop () {
        return MIN_HARDSTOP;
    }
};


class pipeline {
    Mat inputImg, processed, mask, preprocessed;
    ColorMap myColorChoice = ColorMap(COLOR_SELECT);

    float MULTIPLIER = 10;
    float REAL_MIN = INT_INF;
    
    enum FUNCTION_TYPE {
        QUADRATIC, MULTIPLICATIVE
    };
    enum OUTPUT_MODE {
        MASKED, PROCESSED, PREPROCESSED
    };
    
    FUNCTION_TYPE FUNCTION = MULTIPLICATIVE;
    OUTPUT_MODE OUTPUT = MASKED;
    
    
    float cartesian_dist (vector<float> colorArray, vector<uchar> lab_channels) {
//        float difference_1 = pow((colorArray[0] - lab_channels[0]), 2);
        float difference_2 = pow((colorArray[1] - lab_channels[1]), 2);
        float difference_3 = pow((colorArray[2] - lab_channels[2]), 2);
        return sqrt(difference_2 + difference_3);
    }
    
    Mat preprocessor (Mat input) {
        Mat lab_image = input.clone();
        
        // Gaussian blurring
        // GaussianBlur(input, lab_image, Size( 5, 5 ), 0, 0);
        cvtColor(lab_image, lab_image, CV_BGR2Lab);
        return lab_image;
    }
    
    float multiply_dist (float dist) {
        switch (FUNCTION) {
            case QUADRATIC:
                dist = pow(dist, MULTIPLIER);
                break;
            case MULTIPLICATIVE:
                dist *= MULTIPLIER;
                break;
            default:
                break;
        }
        return dist;
    }
    
    
    float cut_off_dist (float dist) {
        if (dist > 255) dist = 255;
        if (dist < 0) dist = 0;
        
        return dist;
    }
    
    
    Mat k_nearest (Mat lab_image) {
        Mat LAB[3];
        split(lab_image, LAB);

        
        Mat img(lab_image.rows, lab_image.cols, CV_8UC1, Scalar(0));
        
        float min = INT_INF;
        float max = 0;
        
        // Main processing
        for (int i = 0; i < lab_image.rows; i++) {
            for (int d = 0; d < lab_image.cols; d++) {
                vector<uchar> lab_channels = {LAB[0].at<uchar>(i, d),
                    LAB[1].at<uchar>(i, d),
                    LAB[2].at<uchar>(i, d)};
                
                float dist = cartesian_dist(myColorChoice.getColor(), lab_channels);
                
                
                if (dist >= myColorChoice.getDistanceLimitFilter()) {
                    // If it is beyond the distance we want then just ignore.
                    dist = 255; // Make it max
                }else{
                    if (dist < REAL_MIN) REAL_MIN = dist;

                    dist = multiply_dist(dist);
                    if (dist < min) min = dist;
                    if (dist > max) max = dist;
                    
                    dist -= MIN_GLOBAL;
                    
                    dist = cut_off_dist(dist);
                }                
                img.at<uchar>(i, d) = 255 - round(dist);
            }
        }
        // TODO: Double check this
        if (REAL_MIN > myColorChoice.getMinHardStop()) {
            // Ignore everything
            img = Mat(lab_image.rows, lab_image.cols, CV_8UC1, Scalar(0));
        } else {
            MAX_GLOBAL = max;
            MIN_GLOBAL = min;
            
            autoAdjust(max, min);
        }

        return img;
    }
    
    void autoAdjust (float max, float min) {
        if (max - min < myColorChoice.getDistanceDifference()) MULTIPLIER += CLIMB;
        if (max - min > myColorChoice.getDistanceDifference()) MULTIPLIER -= CLIMB;
        if (MULTIPLIER < 1) MULTIPLIER = 1;
        if (MULTIPLIER > 10000) MULTIPLIER = 10000;
    }
    
    
    Mat threshold (Mat input) {
        Mat output;
        inRange(input, Scalar(20), Scalar(255), output);
        return output;
    }
    
    
public:
    pipeline (Mat input, float multiplier) {
        // constructor
        time_t start, end;
        time(&start);
        this->MULTIPLIER = multiplier;
        this->inputImg = input;
        this->preprocessed = preprocessor(this->inputImg);
        this->processed = k_nearest(this->preprocessed);
        this->mask = threshold(this->processed);
        
        time(&end);
        double seconds = difftime (end, start);
        
//        cout << (double)seconds << endl;
    }
    
    Mat visualise () {
        switch (OUTPUT) {
            case MASKED:
                return this->mask;
                break;
            case PROCESSED:
                return this->processed;
                break;
            case PREPROCESSED:
                return this->preprocessed;
                break;
            default:
                return this->inputImg;
        }
    }
    
    float getProposedMultipler () {
        return this->MULTIPLIER;
    }
    
    float getRealMin () {
        return this->REAL_MIN;
    }
};


class ImageConverter {
    NodeHandle nh_;
    image_transport::ImageTransport it_;
    image_transport::Subscriber image_sub_;
    image_transport::Publisher image_pub_;

public:
    ImageConverter()
    : it_(nh_) {
        // Subscrive to input video feed and publish output video feed
        image_sub_ = it_.subscribe("asv/camera2/image_color", 1,
        &ImageConverter::imageCb, this);
        image_pub_ = it_.advertise("k_nearest_viewer", 1);

    }

    ~ImageConverter()
    {
    }
    void imageCb(const sensor_msgs::ImageConstPtr& msg) {
        cv_bridge::CvImagePtr cv_ptr;
        try {
            cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);

            if (image_pub_.getNumSubscribers()) {
                resize (cv_ptr->image, cv_ptr->image, Size(), 0.25, 0.25);
                pipeline myPipeline = pipeline(cv_ptr->image, MULTIPLER_GLOBAL);
                // cout << myPipeline.getRealMin() << endl;
                MULTIPLER_GLOBAL = myPipeline.getProposedMultipler();

                // Output modified video stream
                sensor_msgs::ImagePtr output_msg = cv_bridge::CvImage(std_msgs::Header(), "8UC1", myPipeline.visualise()).toImageMsg();
                image_pub_.publish(output_msg);

            } else {
                sensor_msgs::ImagePtr output_msg = cv_bridge::CvImage(std_msgs::Header(), "BGR8", cv_ptr->image).toImageMsg();
                image_pub_.publish(output_msg);
            }
        } catch (cv_bridge::Exception& e) {
            ROS_ERROR("cv_bridge exception: %s", e.what());
            return;
        }
    }
};

void callback(k_nearest::k_nearestConfig &config, uint32_t level) {

    DISTANCE_DIFFERENCE_MANUAL_BOOL = config.distance_difference_manual_mode;
    DISTANCE_DIFFERENCE_MANUAL = config.distance_difference;

    DISTANCE_LIMIT_FILTER_MANUAL_BOOL = config.distance_limit_filter_manual_mode;
    DISTANCE_LIMIT_FILTER_MANUAL = (float) config.distance_limit_filter;

    if (DISTANCE_DIFFERENCE_MANUAL_BOOL) {
        ROS_INFO("Setting distance difference: %d", 
            config.distance_difference);

        ROS_INFO("Setting distance limit filter: %lf", 
            config.distance_limit_filter);
    }

    MANUAL_COLORS_BOOL = config.manual_color_set;

    if (MANUAL_COLORS_BOOL) {
        MANUAL_L = config.L;
        MANUAL_A = config.A;
        MANUAL_B = config.B;
    } else {
        switch (config.color_selection) {
            case 0:
                COLOR_SELECT = "PURE_RED";
                break;
            case 1: 
                COLOR_SELECT = "PURE_GREEN";
                break;
            case 2: 
                COLOR_SELECT = "PURE_BLUE";
                break;
            case 3:
                COLOR_SELECT = "WEIRD_RED";
                break;
            case 4:
                COLOR_SELECT = "WEIRD_GREEN";
                break;
            case 5: 
                COLOR_SELECT = "WEIRD_BLUE";
                break;
            case 6:
                COLOR_SELECT = "WEIRD_YELLOW";
                break;
            case 7:
                COLOR_SELECT = "BRIGHTER_BLUE";
                break;
            case 8:
                COLOR_SELECT = "PURE_YELLOW";
                break;
        }
        ROS_INFO("Setting detection to detect: %s", COLOR_SELECT.c_str());
    }

    CLIMB = config.speed_of_adjustment;
    ROS_INFO("Setting adjustment speed: %lf", CLIMB);
}

int main(int argc, char** argv)
{
    if (argc > 1) {
        vector<string> args(argv, argv + argc);
        cout << args[1] << endl;
        COLOR_SELECT = args[1];
    }
    init(argc, argv, "k_nearest_detector");
    ImageConverter ic;

    dynamic_reconfigure::Server<k_nearest::k_nearestConfig> server;
    dynamic_reconfigure::Server<k_nearest::k_nearestConfig>::CallbackType f;

    f = boost::bind(&callback, _1, _2);
    server.setCallback(f);


    spin();
    return 0;
}
