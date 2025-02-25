//
// Created by filippo on 07/07/22.
//

#include "Util.h"
#include <fstream>
#include <iostream>
#include <chrono>

using namespace cv;
using namespace std;
using namespace hand_detect;


double hand_detect::IoU_score(Rect detected, Rect ground_truth) {
    Rect I = detected & ground_truth;
    Rect U = detected | ground_truth;
    return static_cast<double>(I.area())/static_cast<double>(U.area());
}

double hand_detect::pixel_accuracy(Mat const& detected, Mat const& ground_truth) {
    CV_Assert(detected.type()==CV_8UC1);
    CV_Assert(ground_truth.type()==CV_8UC1);
    CV_Assert(detected.size() == ground_truth.size());

    double count_different = countNonZero(detected ^ ground_truth);
    return 1 - (count_different / (detected.rows * detected.cols));
}

vector<Rect> hand_detect::extract_bboxes(string const& txt_path, double scale_factor) {
    CV_Assert(scale_factor > 0);

    ifstream boxes_txt = ifstream(txt_path);
    if(!boxes_txt.is_open()) {
        cerr << "File not found";
        return vector<Rect>{};
    }

    /*
     * Let fractional_padding = f_p
     * new_area = f_p * area
     * So:
     *
     * new_w = sqrt(f_p) * w
     * new_w = w + 2*pad_x
     *
     * SOLVE FOR pad_x:
     * pad_x = w*(sqrt(f_p)-1)/2
     *
     * similarly for h and pad_y.
     *
     * Note that when pad > 0 we have f_p > 1 (enlarging)
     * when pad < 0 we have f_p < 1 (shrinking)
     */
    vector<Rect> boxes;
    while(!boxes_txt.eof()) {
        int x=-1, y=-1, w=-1, h=-1; //params of the rectangle to be read from file
        boxes_txt >> x >> y >> w >> h;
        if(x<0 || y<0 || w<0 || h<0)
            continue; //managing spurious lines at the end of the file
        int padding_x = cvRound(0.5 * w * (sqrt(scale_factor)-1));
        int padding_y = cvRound(0.5 * h * (sqrt(scale_factor)-1));
        boxes.emplace_back(x-padding_x, y-padding_y, w+(2*padding_x), h+(2*padding_y));
    }
    boxes_txt.close();
    return boxes;
}

void hand_detect::show_bboxes(string const& img_path, string const& txt_path) {
    Mat input = imread(img_path);
    vector<Rect> boxes = extract_bboxes(txt_path);

    for(auto const& r : boxes)
        rectangle(input, r, Scalar{0,0,255});

    imshow("", input);
    waitKey(0);
}

void hand_detect::drawGrabcutMask(Mat const& image, Mat const& mask, Mat& output, float transparency_level) {
    CV_Assert(transparency_level <= 1 && transparency_level >= 0);
    Scalar FG_COLOR{255, 255, 255};
    Scalar PROB_FG_COLOR{0,255,0};

    Scalar BG_COLOR{0, 0, 255};
    Scalar PROB_BG_COLOR{255,0,0};

    Mat colored_mask{mask.size(), CV_8UC3};

    colored_mask.setTo(BG_COLOR, mask==0);
    colored_mask.setTo(FG_COLOR, mask==1);
    colored_mask.setTo(PROB_BG_COLOR, mask==2);
    colored_mask.setTo(PROB_FG_COLOR, mask==3);

    output.create(image.size(), CV_8UC3);
    addWeighted(image, transparency_level, colored_mask, 1-transparency_level, 0, output);
}

double hand_detect::avg_IoU_score(vector<Rect> &detected, vector<Rect> &ground_truth, double threshold) {
    struct Score {
        double IoU = 0;
        int det_index = -1;
    };

    double avg_IoU = 0;
    int tp = 0;
    int fp = static_cast<int>(detected.size());
    int fn = 0;

    vector<vector<double>> all_IoU_scores(ground_truth.size());
    vector<Score> IoU_scores(ground_truth.size());

    for (vector<double> &det_vec : all_IoU_scores) {
        det_vec = vector<double>(detected.size());
    }

    // Compute all IoU scores for ground_truth x detected
    for (int i = 0; i < ground_truth.size(); i++) {
        for (int j = 0; j < detected.size(); j++) {
            all_IoU_scores[i][j] = IoU_score(detected[j], ground_truth[i]);
        }
    }

    for (int c_gt = 0; c_gt < ground_truth.size(); c_gt++) {
        double max_IoU = 0;
        int gt_index = -1;
        int det_index = -1;

        // find maximum IoU in matrix all_IoU_scores
        for (int i = 0; i < ground_truth.size(); i++) {
            for (int j = 0; j < detected.size(); j++) {
                if (all_IoU_scores[i][j] > max_IoU) {
                    max_IoU = all_IoU_scores[i][j];
                    gt_index = i;
                    det_index = j;
                }
            }
        }

        if (max_IoU < threshold) {
            break;
        }

        if (IoU_scores[gt_index].det_index == -1) {
            IoU_scores[gt_index].det_index = det_index;
            IoU_scores[gt_index].IoU = all_IoU_scores[gt_index][det_index];

            cout << "-- IoU score for hand " << gt_index << ": " << IoU_scores[gt_index].IoU << "\n";

            avg_IoU += IoU_scores[gt_index].IoU;

            fp--;
            tp++;
        }

        for (int i = 0; i < ground_truth.size(); i++) {
            all_IoU_scores[i][det_index] = -1;
        }
    }

    fn = static_cast<int>(IoU_scores.size()) - tp;
    avg_IoU /= tp + fp + fn;

    return avg_IoU;
}

bool hand_detect::is_monochromatic(cv::Mat const& input) {
    CV_Assert(input.type() == CV_8UC3);
    Mat hsv;
    cvtColor(input, hsv, COLOR_BGR2HSV_FULL);

    Mat channels[3];
    split(hsv, channels);

    int count0 = countNonZero(channels[0] != channels[0].at<uchar>(0,0));
    int count1 = countNonZero(channels[1] != channels[1].at<uchar>(0,0));

    return (count0 + count1 == 0);
}

void hand_detect::loadImages(vector<Mat>& images, string const& folder_path, vector<std::string>& images_names) {
    vector<std::string> img_names;
    glob(folder_path, img_names, false);

    for (std::string& img_name : img_names) {
        images.push_back(imread(img_name));

        std::string image_name = img_name.substr(folder_path.size() - 5, img_name.size() - folder_path.size() + 5);
        images_names.push_back(image_name);
    }
}

void hand_detect::loadBoundingBoxes(vector<vector<Rect>> &bounding_boxes, string const& folder_path) {
    vector<std::string> file_names;
    glob(folder_path, file_names, false);

    for (String& file_name : file_names) {
        bounding_boxes.push_back(extract_bboxes(file_name));
    }
}

void hand_detect::crop_bboxes(cv::Mat const& input, cv::Rect& box) {
    if(box.x < 0)
        box.x = 0;
    if(box.y < 0)
        box.y = 0;
    if(input.cols < (box.x + box.width))
        box.width = input.cols - box.x ;
    if(input.rows < (box.y + box.height))
        box.height = input.rows - box.y ;
}
