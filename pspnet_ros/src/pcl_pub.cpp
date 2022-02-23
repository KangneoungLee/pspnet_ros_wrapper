#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>

#include <boost/bind.hpp>
#include <iostream>

#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <pcl/conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <pcl_ros/point_cloud.h>
#include <pcl_conversions/pcl_conversions.h>

#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/Image.h>


/* parameters for generating point cloud from  image */
#define STRIDE 3
#define STRIDE1 5
#define STRIDE2 10

#define ROWTHRESH 240
#define ROWTHRESH1 280
#define ROWTHRESH2 320

#define DEPTH_FAR_THRESH 6


typedef pcl::PointCloud<pcl::PointXYZI> cloudxyzi_t;


class Pclpub{

private:

   float _fx;
   float _fy;
   float _px;
   float _py;
   float _dscale;   

   int _image_rows;
   int _image_cols;

   std::string _point_cloud_frame;
   std::string _tf_prefix;

   bool _camerainfo_receive_flag = false;

   ros::NodeHandle m_nh;
   ros::NodeHandle p_nh;
   ros::Rate* _loop_rate;

   cv::Mat _cost_image;
   cv::Mat _sync_depth_image;
  
   ros::Subscriber _depth_cam_info_sub;

   message_filters::Subscriber<sensor_msgs::Image> _cost_img_sub;
   message_filters::Subscriber<sensor_msgs::Image> _sync_depth_img_sub;

   typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::Image> MySyncPolicy;
   typedef message_filters::Synchronizer<MySyncPolicy> Sync;
   boost::shared_ptr<Sync> _sync;

public:
   Pclpub(ros::NodeHandle nh, ros::NodeHandle _nh);
   ~Pclpub ();

   void costmap_img_callback(const sensor_msgs::Image::ConstPtr& msg);
   void sync_depth_img_callback(const sensor_msgs::Image::ConstPtr& msg);
   void costmap_sync_depth_img_callback(const sensor_msgs::Image::ConstPtr& cost_img,const sensor_msgs::Image::ConstPtr& depth_img);
   void depth_cam_info_callback(const sensor_msgs::CameraInfo::ConstPtr& msg);

   void run();

};

Pclpub::Pclpub(ros::NodeHandle nh, ros::NodeHandle _nh):m_nh(nh),p_nh(_nh)
{
     std::string cost_image_topic ="pspnet_output/cost_image";
     std::string sync_depth_image_topic = "pspnet_output/sync_depth_raw";
     std::string depth_cam_info_topic = "camera/depth/camera_info";
     std::string point_cloud_frame = "camera_optical_frame";
     std::string tf_prefix = "";

     int update_rate = 10;

     p_nh.getParam("cost_image_topic",cost_image_topic);
     p_nh.getParam("sync_depth_image_topic",sync_depth_image_topic);
     p_nh.getParam("depth_cam_info_topic",depth_cam_info_topic);
     p_nh.getParam("point_cloud_frame",point_cloud_frame);
     p_nh.getParam("tf_prefix",tf_prefix);
     
     //p_nh.getParam("update_rate",update_rate);


     _point_cloud_frame = point_cloud_frame;
     _tf_prefix = tf_prefix;

     _cost_img_sub.subscribe(m_nh, cost_image_topic ,1);
     _sync_depth_img_sub.subscribe(m_nh, sync_depth_image_topic ,1);
     
     _sync.reset(new Sync(MySyncPolicy(10), _cost_img_sub, _sync_depth_img_sub));
     _sync->registerCallback(boost::bind(&Pclpub::costmap_sync_depth_img_callback, this, _1, _2));

     _depth_cam_info_sub = m_nh.subscribe<sensor_msgs::CameraInfo>(depth_cam_info_topic,1,&Pclpub::depth_cam_info_callback,this);

     this->_loop_rate = new ros::Rate(update_rate);

}

Pclpub::~Pclpub()
{
   delete this->_loop_rate;
}

void Pclpub::costmap_img_callback(const sensor_msgs::Image::ConstPtr& msg)
{
  cv::Mat cost_image;
  cost_image = cv_bridge::toCvShare(msg)->image;
  this->_cost_image = cost_image.clone();
}

void Pclpub::sync_depth_img_callback(const sensor_msgs::Image::ConstPtr& msg)
{
  cv::Mat sync_depth_image;
  sync_depth_image = cv_bridge::toCvShare(msg)->image;
  this->_sync_depth_image = sync_depth_image.clone();
}

void Pclpub::depth_cam_info_callback(const sensor_msgs::CameraInfo::ConstPtr& msg)
{
  this->_fx = msg->K[0];
  this->_fy = msg->K[4];
  this->_px = msg->K[2];
  this->_py = msg->K[5];
  this->_dscale = 0.001; //this->_dscale = msg->D[0];

  this->_camerainfo_receive_flag = true;
}

void Pclpub::costmap_sync_depth_img_callback(const sensor_msgs::Image::ConstPtr& cost_img,const sensor_msgs::Image::ConstPtr& depth_img)
{
  cv::Mat sync_depth_image;
  cv::Mat cost_image;

  cost_image = cv_bridge::toCvCopy(cost_img)->image;
  cost_image.convertTo(cost_image,CV_16UC1);
  sync_depth_image = cv_bridge::toCvShare(depth_img)->image;  /* 1 channel uint8 image can not be read properly so type conversion is needed*/
  
  this->_image_rows = cost_image.rows;
  this->_image_cols = cost_image.cols;
  
  //std::ofstream in;
  //in.open("cost_image_check.txt");
  //int i,j;
  //for (i=0;i<cost_image.rows;i++)
  //{
  //  for(j=0;j<cost_image.cols;j++)
  //  {
  //    in<<cost_image.at<unsigned short>(i,j)<<" "; 
  //  }
  //    in<<std::endl;
  //}
  //in.close();

  //std::cout<<"rows: "<<cost_image.rows<<"cols: "<<cost_image.cols<<"types : "<<cost_image.type()<<cost_image.at<unsigned short>(200,200)<<std::endl;
  //std::cout<<"rows: "<<sync_depth_image.rows<<"cols: "<<sync_depth_image.cols<<"types : "<<sync_depth_image.type()<<sync_depth_image.at<unsigned short>(200,200)<<std::endl;

  this->_cost_image = cost_image.clone();
  this->_sync_depth_image = sync_depth_image.clone();


  if(this->_camerainfo_receive_flag == false)
  {
      ROS_WARN("Cant not receive the CameraInfo Message. Camera parameters are manually assigned");
      this->_fx = 425;
      this->_fy = 425;
      this->_px = 423;
      this->_py = 239;
      this->_dscale = 0.001; 

       
  }
}


void Pclpub::pointcloud_publish()
{
   
   unsigned short colstride;
   unsigned short rowstride;
   unsigned short row_thresh, row_thresh1, row_thresh2;
   float depth_far_thresh;
   float x,y,z;

   cv::Mat float_sync_depth_image;

   row_thresh = ROW_THRESH;
   row_thresh1 = ROW_THRESH1;
   row_thresh2 = ROW_THRESH2;

   colstride = STRIDE;
   rowstride = STRIDE;

   depth_far_thresh = (float)DEPTH_FAR_THRESH;

   if(this->_sync_depth_image.type() == CV_16UC1)
   {
      this->_sync_depth_image.convertTo(float_sync_depth_image,CV_32F,this->_dscale);
   }
   else if(this->_sync_depth_image.type() == CV_32F)
   {
      float_sync_depth_image = this->_sync_depth_image.clone(); 
   }

   int row_step,col_step;

   float depth;
   float cost_value;
   
   for(row_step = row_thresh; row_step<this->_image_rows; row_step = row_step + rowstride)
   {

     if(row_step >= row_thresh2)
     {
        colstride = STRIDE2;
        rowstride = STRIDE2;
     }
     else if(row_step >= row_thresh1)
     {
        colstride = STRIDE1;
        rowstride = STRIDE1;
     }


     for(col_step = 0; col_step<this->_image_cols; col_step = col_step + colstride)
     {
        depth = float_sync_depth_image.at<float>(row_step,col_step);
        cost_value = this->_cost_image.at<unsigned short>(row_step,col_step);
        
        /* depth is too far or too close then the point of ground is not reliable, cost value is 0 then no need to publish the point */
        if((depth > depth_far_thresh)||(depth < 0.1)||(cost_value == 0)) continue;

        x = (col_step - this->_px)*depth/this->fx;
        y = (row_step - this->_py)*depth/this->fy;
        z = depth;
        
     }
   }


}

void Pclpub::run()
{
   while(ros::ok)
   {
     ros::spinOnce();
     this->_loop_rate->sleep();
   }

}


int main(int argc, char** argv)
{
  ros::init(argc,argv,"pspnet_pcl_publish");
  ros::NodeHandle nh;
  ros::NodeHandle _nh("~");

  Pclpub pclpub(nh,_nh);

  pclpub.run();

  return 0;
}

