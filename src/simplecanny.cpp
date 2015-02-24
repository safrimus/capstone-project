#include <stdio.h>
#include <iostream>
#include <cmath>
#include <vector>
#include <math.h>

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/Int32.h>
#include <ardrone_autonomy/image.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>

#include <opencv/cv.h>
#include <opencv/cxcore.h>
#include <opencv/highgui.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <cv_bridge/cv_bridge.h>
 
/*here is a simple program which demonstrates the use of ros and opencv to do image manipulations on video streams given out as image topics from the monocular vision
of robots,here the device used is a ardrone(quad-rotor).*/
 
using namespace std;
using namespace cv;
namespace enc = sensor_msgs::image_encodings;
 
static const char WINDOW[] = "Image window";
 
class simplecanny
{
  ros::NodeHandle nh_;
  ros::NodeHandle n;
  ros::Publisher pix_pub;

  image_transport::ImageTransport it_;    
  image_transport::Subscriber image_sub_; //image subscriber 
  image_transport::Publisher image_pub_; //image publisher(we subscribe to ardrone image_raw)

public:
	simplecanny(): it_(nh_){
		image_sub_ = it_.subscribe("/ardrone/image_raw", 1, &simplecanny::imageCb, this);
		image_pub_= it_.advertise("/arcv/Image",1);
		pix_pub= n.advertise<ardrone_autonomy::image>("dpix_pub",1);
		cv::namedWindow(WINDOW);
	}
 
  ~simplecanny(){
    cv::destroyWindow(WINDOW);
  }
 
  void imageCb(const sensor_msgs::ImageConstPtr& msg){

    /* OpenCV image pointer, copy message and point to copied image */
    cv_bridge::CvImagePtr cv_imgptr; 
    cv_imgptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8); 
 
    /* Create cascade classifier, loaded with XML file */
    const char* cascade_name = "/home/odroid/catkin_ws/src/ardrone_autonomy/XML/lbpcascade_frontalface.xml";
    cv::CascadeClassifier cascade;
    cascade.load(cascade_name);
    
    /* Create new CV image w/ grayscale */
    cv::Size image_size(cv_imgptr->image.cols, cv_imgptr->image.rows);
    cv::Mat gray = cv::Mat(image_size, CV_8UC1);
    cv::cvtColor(cv_imgptr->image, gray, CV_BGR2GRAY);

    /*Create vector of type Rect and then use cascade classifier to detect all rects and store in faces */
    vector<cv::Rect> faces;
    faces.clear();
    cascade.detectMultiScale(gray, faces, 1.2, 3, 0, cv::Size( 40, 40));
    
    vector<int> face_area;
	ardrone_autonomy::image msg_pub;
	msg_pub.header.frame_id= "Image";
	msg_pub.pixels = 0;

	/* Go through each face -- use of iterator to go through vector */
	for (int i = 0; i < faces.size(); i++){
		CvPoint ul; CvPoint lr;
		ul.x = faces[i].x; ul.y = faces[i].y;
		lr.x = faces[i].x + faces[i].width; lr.y = faces[i].y + faces[i].height;
		face_area.push_back((lr.x-ul.x)*(ul.y-lr.y));
	}

	/* Vector of ints to hold area of detected face and iterator of same type */
	//vector<int>::iterator it;

	static cv::Scalar RED = cv::Scalar(0, 0, 255);
	int max = 0;
	int index = 0;
 
	for (int i = 0; i < face_area.size(); ++i){
	  if (face_area[i] > max)
		index = i;
	}

	/* Iterate through all faces found again, */
	if (faces.size()>0){
	  CvPoint ul_final; CvPoint lr_final;
	  ul_final.x = faces[index].x; ul_final.y = faces[index].y; 
	  lr_final.x = ul_final.x + faces[index].width; lr_final.y = ul_final.y + faces[index].height;
	
	  cv::rectangle(cv_imgptr->image, ul_final, lr_final, RED, 3, 8, 0);
	  
	  msg_pub.header.stamp = ros::Time::now();
	  msg_pub.pixels = (((lr_final.x+ul_final.x)/2) - ((cv_imgptr->image.cols)/2));
	  ROS_INFO("lr.x: %d, ur.x: %d", lr_final.x,ul_final.x);
	  ROS_INFO("msgvalue:%d", msg_pub.pixels);
	  pix_pub.publish(msg_pub);
	}

    cv::imshow(WINDOW,cv_imgptr->image);
    cv::waitKey(2);
}
};
 
 
int main(int argc, char** argv)
{
	ros::init(argc, argv, "image_processing");
	simplecanny processing_obj;

	ros::spin();
 
	return 0;
}