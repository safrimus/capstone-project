//Capstone Project: Autonomous Action-Sport Camera Drone
//Error Calculator Module
//Purpose: Central module that takes inputs from the image processing module and calculates the speed & direction
//		   of the drone.
//	Input: Distance (cm), horizontal pixel deviation and vertical pixel deviation (from center of image)
//	Output: Forward/back, vertical and yaw speed

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Empty.h>
#include <std_msgs/Bool.h>
#include <ardrone_autonomy/image.h>
#include <ardrone_autonomy/Navdata.h>
#include <geometry_msgs/Twist.h>

using namespace std;

void sigint_handler(int sig) {
	ros::NodeHandle nh;
	ros::Publisher land;
	std_msgs::Empty empty_msg;
	
	land = nh.advertise<std_msgs::Empty>("/ardrone/land", 1, true);
	usleep(2*1000*1000);
	ROS_INFO("INFO: Landing!");
	land.publish(empty_msg);
	
	ros::shutdown();
}

void move_drone(ros::Publisher move_drone_pub, float velocity_x, float velocity_z, float yaw_speed) {
	geometry_msgs::Twist msg;
	msg.linear.x = velocity_x;
	msg.linear.y = 0;
	msg.linear.z = velocity_z;
	msg.angular.x = 0;
	msg.angular.y = 0;
	msg.angular.z = yaw_speed;

	move_drone_pub.publish(msg);
}

////////////////////////////////////////////////////////////////////////////////
// class increase_altitude
////////////////////////////////////////////////////////////////////////////////

class increase_altitude {
	int current_altitude;
	int desired_altitude;
	ros::Publisher move_drone_pub;
	ros::NodeHandle nh;
	
	public:
		increase_altitude(ros::Publisher move_drone_pub_in, ros::NodeHandle n, int altitude) {
			current_altitude = 0;
			desired_altitude = altitude;
			move_drone_pub = move_drone_pub_in;
			nh = n;
		}
		
		void callback(const ardrone_autonomy::Navdata::ConstPtr& msg) {
			//read altitude data from the drone
			current_altitude = msg->altd;
			move_drone(move_drone_pub, 0, 0.6, 0);
		}
		
		void run() {
			ros::Subscriber navdata_altitude;
			
			ROS_INFO("INFO: Increasing altitude to %d...", desired_altitude);
			
			//subscribe to the navdata topic published by the drone
			navdata_altitude = nh.subscribe<ardrone_autonomy::Navdata> ("/ardrone/navdata", 1, &increase_altitude::callback, this);
			
			while (current_altitude < desired_altitude)
				ros::spinOnce();
			
			move_drone(move_drone_pub, 0, 0, 0);
			navdata_altitude.shutdown();
			
			ROS_INFO("INFO: Altitude is now %d", current_altitude);
			return;
		}
};

////////////////////////////////////////////////////////////////////////////////
// class PID_controller
////////////////////////////////////////////////////////////////////////////////

class PID_controller {
	double time_stamp_prev;
	float integral;
	float SP; 								//Set point
	float kp, ki, kd;						//Gain values for PID controller
	float outMax, outMin;					//Max values for the PID output
	float prev_error[3];					//Stores previous error values for calculating derivative
	float output_slew_rate, prev_output;
	float offset;
	
	public:
		PID_controller(float SP_in, float slew_rate_in, float outMax_in, float outMin_in, float kp_in, float ki_in, float kd_in, float offset_in) {
			time_stamp_prev = 0;
			integral = 0;
			kp = kp_in; 
			ki = ki_in; 
			kd = kd_in;
			SP = SP_in;
			outMax = outMax_in;
			outMin = outMin_in;
			prev_error[0] = 0; 
			prev_error[1] = 0; 
			prev_error[2] = 0;
			output_slew_rate = slew_rate_in;
			prev_output = 0;
			offset = offset_in;
		}
		
		float compute_output(float PV){
			float error, dt, output;
			
			double time_stamp_curr = ros::Time::now().toSec();
			dt = time_stamp_curr - time_stamp_prev;
			
			//compute error
			error = SP - PV;
			
			//compute integral term
			integral += error * dt;
			
			//compute derivative term
			float derivative = ((error + 3*prev_error[2] - 3*prev_error[1] - prev_error[0])/6)/dt;
			
//			ROS_INFO("Error: [%f] , Integral: [%f], Derivative: [%f]", error, integral, derivative);
//			ROS_INFO("dt: [%f]", dt);
			
			//compute output if error is more than offset
			if (error > offset || error < (offset*(-1)))
				output = (kp*error) + (ki*integral) + (kd*derivative);
			else
				output = 0;
			
			//output slew rate
			if (output-prev_output > output_slew_rate) output = prev_output+output_slew_rate;
			else if (output-prev_output < -output_slew_rate) output = prev_output-output_slew_rate;
			//cap output
			if (output > outMax) output = outMax;
			else if(output < outMin) output = outMin;
			
			time_stamp_prev = time_stamp_curr;
			prev_output = output;
			prev_error[1] = prev_error[0];
			prev_error[2] = prev_error[1];
			prev_error[0] = error;
			
			return output;
		}
};

////////////////////////////////////////////////////////////////////////////////
// class autonomy
////////////////////////////////////////////////////////////////////////////////

class autonomy {
	ros::NodeHandle n;
	ros::Publisher move_drone_pub;
	ros::Publisher ready_pub;
	ros::Publisher takeoff_pub;
	ros::Subscriber image_pro;
	
	int takeoff_altitude;
	
	PID_controller velocity_PI;
	PID_controller yaw_PI;
	PID_controller alt_PI;
	
	public:
		autonomy(int alt, PID_controller &velocity_PI_in, PID_controller &yaw_PI_in, PID_controller &alt_PI_in)
		:velocity_PI(velocity_PI_in)
		,yaw_PI(yaw_PI_in)
		,alt_PI(alt_PI_in)
		{
					 
			//connect to takeoff topic
			takeoff_pub = n.advertise<std_msgs::Empty>("/ardrone/takeoff", 1, true);
			//connect to the cmd_vel topic
			move_drone_pub = n.advertise<geometry_msgs::Twist>("cmd_vel", 1000);
			//connect to the ready topic
			ready_pub = n.advertise<std_msgs::Bool>("ready",1);
			
			takeoff_altitude = alt;
			velocity_PI = velocity_PI_in;
			yaw_PI = yaw_PI_in;
			alt_PI = alt_PI_in;
		}
				
		void initialize() {	
			std_msgs::Empty empty_msg;
			
			ROS_INFO("INFO: Flat trimming...ensure drone is on a flat surface!");
			system("rosservice call /ardrone/flattrim");

			ROS_INFO("INFO: Taking Off!");
			takeoff_pub.publish(empty_msg);
			
			usleep(3*1000*1000);					//Wait for drone to complete take-off
			move_drone(move_drone_pub, 0, 0, 0);
			
			//increase altitude of drone from default (1m)
			increase_altitude increase(move_drone_pub, n, takeoff_altitude);
			increase.run();
		}
		
		void ready() {
			std_msgs::Bool ready_sig;
			ready_sig.data = true;
			ready_pub.publish(ready_sig);
		}
		
		void callback(const ardrone_autonomy::image::ConstPtr& image) {
			//calculate forward speed
			float x_velocity = -velocity_PI.compute_output(image->distance);
			ROS_INFO("----->x_velocity: [%f]<-----, Distance: [%f]\n", x_velocity, image->distance);

			//calculate yaw speed
			float yaw_speed = yaw_PI.compute_output(image->yaw);
			ROS_INFO("----->Yaw Speed: [%f]<------, Horizontal: [%f]\n", yaw_speed, image->yaw);
			
			//calculate vertical speed
			float z_velocity = alt_PI.compute_output(image->height);
			ROS_INFO("----->z_velocity: [%f]<------, Vertical: [%f]\n", z_velocity, image->height);

			move_drone(move_drone_pub, x_velocity, z_velocity, yaw_speed);
		}
		
		void tracking() {
			//connect to the image_data topic
			image_pro = n.subscribe<ardrone_autonomy::image> ("image_data", 100, &autonomy::callback, this);
			
			ros::spin();
		}
};

////////////////////////////////////////////////////////////////////////////////
// main
////////////////////////////////////////////////////////////////////////////////

int main (int argc, char **argv) {
	ros::init(argc, argv, "error", ros::init_options::NoSigintHandler);
	signal(SIGINT, sigint_handler);
	
	ROS_INFO("1: [%f], 2: [%f], 3: [%f]", atof(argv[1]), atof(argv[2]), atof(argv[3]));
	ROS_INFO("4: [%f], 5: [%f], 6: [%f]", atof(argv[4]), atof(argv[5]), atof(argv[6]));
	ROS_INFO("7: [%f], 8: [%f], 9: [%f]", atof(argv[7]), atof(argv[8]), atof(argv[9]));
	
	//initialize PID_controller objects
	//PID_controller(float SP_in, float slew_rate_in, float outMax_in, float outMin_in, float kp_in, float ki_in, float kd_in, float offset_in)
	PID_controller altitude_PI(0, 0.5, 0.4, -0.4, atof(argv[1]), atof(argv[2]), atof(argv[3]), atof(argv[4]));
	PID_controller velocity_PI(250, 0.2, 0.3, -0.3, atof(argv[5]), atof(argv[6]), atof(argv[7]), atof(argv[8]));
	PID_controller yaw_PI     (0, 0.2, 0.5, -0.5, atof(argv[9]), atof(argv[10]), atof(argv[11]), atof(argv[12]));
	
	autonomy drone(1300, velocity_PI, yaw_PI, altitude_PI);
	
	//start drone
	drone.initialize();
	
	//send ready signal to image_pro
	drone.ready();
	
	//start tracking
	drone.tracking();

	return 0;
}
