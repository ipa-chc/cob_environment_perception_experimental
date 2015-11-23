#pragma once

#define BOOST_SIGNALS_NO_DEPRECATION_WARNING

#include <cob_3d_mapping_geometry_map_v2/types/context.h>
#include <cob_3d_mapping_geometry_map_v2/visualization/marker.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>

class GeometryNode {
	ros::NodeHandle nh_;
	cob_3d_geometry_map::GlobalContext ctxt_;

	void callback(const cob_3d_mapping_msgs::PlaneSceneConstPtr& scene, const sensor_msgs::ImageConstPtr& color_img)
	{
	  ROS_INFO("callback");
	  
	  ctxt_.add_scene(*scene);
	}

	void callback2(const cob_3d_mapping_msgs::PlaneSceneConstPtr& scene)
	{
	  ROS_INFO("callback2");
	  
	  cob_3d_visualization::RvizMarkerManager::get().setFrameId(scene->header.frame_id);
	  
	  ctxt_.add_scene(*scene);
	  ctxt_.visualize_markers();
	  
	  system("read x");
	}

	void cb_camera_info(const sensor_msgs::CameraInfoConstPtr& camera_info)
	{
	  ROS_INFO("camera_info");
	  
	  camera_info_ = *camera_info;
	  sub_camera_info_.shutdown();	//just get one message (assume camera does not change while running)
	  
	  Eigen::Matrix3d P;
	  std::copy(camera_info_.K.begin(), camera_info_.K.end(), P.data());
	  P = P.transpose().eval();
	  std::cout<<"P\n"<<P<<std::endl;
	  std::cout<<"Pi\n"<<P.inverse()<<std::endl;
	  P.row(0) /= camera_info_.width;
	  P.row(1) /= camera_info_.height;
	  
	  ctxt_.set_projection(P);
	  
	  start();
	}
	
	typedef message_filters::sync_policies::ApproximateTime<cob_3d_mapping_msgs::PlaneScene, sensor_msgs::Image> TSyncPolicy;
	
	boost::shared_ptr<message_filters::Subscriber<cob_3d_mapping_msgs::PlaneScene> > sub_scene_;
	boost::shared_ptr<message_filters::Subscriber<sensor_msgs::Image> > sub_col_img_;
	boost::shared_ptr<message_filters::Synchronizer<TSyncPolicy> > sync_;
	
	ros::Subscriber sub_scene2_, sub_camera_info_;
	
	sensor_msgs::CameraInfo camera_info_;
	
	void init_context() {
		//register default classifiers
		
		Eigen::Vector3f floor_offset(0,0.5f,0);
		Eigen::Vector3f floor_orientation = Eigen::Vector3f::UnitY();
		
		ctxt_.registerClassifier(new cob_3d_geometry_map::DefaultClassifier::Classifier_Floor(floor_orientation, floor_offset, 0.1f, 0.1f));
	}
	
	void start() {
		init_context();
		
		//now start the ROS stuff
		sub_scene_.reset(new message_filters::Subscriber<cob_3d_mapping_msgs::PlaneScene>(nh_, "scene", 1));
		sub_col_img_.reset(new message_filters::Subscriber<sensor_msgs::Image>(nh_, "color_image", 1));
		
		sub_scene2_ = nh_.subscribe("scene", 1, &GeometryNode::callback2, this);
		 
		// ApproximateTime takes a queue size as its constructor argument, hence MySyncPolicy(10)
		sync_.reset(new message_filters::Synchronizer<TSyncPolicy>(TSyncPolicy(10), *sub_scene_, *sub_col_img_));
		sync_->registerCallback(boost::bind(&GeometryNode::callback, this, _1, _2));
	}
	
public:

	GeometryNode() {
		sub_camera_info_ = nh_.subscribe("camera_info", 1, &GeometryNode::cb_camera_info, this);
	}


};
