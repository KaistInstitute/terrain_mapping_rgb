#include <ros/ros.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <math.h>
#include <chrono>
#include <actionlib/server/simple_action_server.h>
#include <tf/transform_broadcaster.h>
#include <gogo_gazelle/MotionAction.h>
#include <math.h>
#include "gogo_gazelle/update.h"
#include "lanros2podo.h"

#define D2R             0.0174533
#define R2D             57.2958
#define robot_idle      1
#define robot_paused    2
#define robot_moving    3
#define real_mode       0
#define simul_mode      1
#define PORT1 6000
#define PORT2 6001
#define IPAddr "192.168.0.30"

int sock_status = 0, valread;
int sock_result = 0;
//char buffer[1024] = {0};
struct sockaddr_in ROSSocket;
struct sockaddr_in RSTSocket;
LANROS2PODO TX;
P2R_status RX;
P2R_result RXresult;
int RXDataSize;
void* RXBuffer;
int RXResultDataSize;
void* RXResultBuffer;

pthread_t THREAD_t;

ros::Publisher robot_states_pub;
ros::Publisher marker_tf_pub;
gogo_gazelle::update message;


float marker_x= 0.,marker_y= 0.,marker_z= 0.,marker_wx= 0.,marker_wy= 0.,marker_wz = 0.;
float marker_w = 1.;
float shelf_x= 0., shelf_y= 0.,shelf_z= 0.,shelf_wx= 0.,shelf_wy= 0.,shelf_wz = 0.;
float shelf_w = 1.;

bool connectROS()
{
    if((sock_status = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error creating socket \n");
        return false;
    }


    ROSSocket.sin_family = AF_INET;
    ROSSocket.sin_port = htons(PORT1);

    int optval = 1;
    setsockopt(sock_status, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    setsockopt(sock_status, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    if(inet_pton(AF_INET, IPAddr, &ROSSocket.sin_addr)<=0)
    {
        printf("\n Invalid Address \n");
        return false;
    }
    RXDataSize = sizeof(P2R_status);
    RXBuffer = (void*)malloc(RXDataSize);

    if(connect(sock_status, (struct sockaddr *)&ROSSocket, sizeof(ROSSocket)) < 0)
    {
        printf("\n Connection failed \n");
        return false;
    }

    printf("\n Client connected to server!(ROS)\n");
    return true;
}

bool connectRST()
{
    if((sock_result = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error creating socket \n");
        return false;
    }

    RSTSocket.sin_family = AF_INET;
    RSTSocket.sin_port = htons(PORT2);

    int optval = 1;
    setsockopt(sock_result, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    setsockopt(sock_result, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    if(inet_pton(AF_INET, IPAddr, &RSTSocket.sin_addr)<=0)
    {
        printf("\n Invalid Address \n");
        return false;
    }

    RXResultDataSize = sizeof(P2R_result);
    RXResultBuffer = (void*)malloc(RXResultDataSize);

    if(connect(sock_result, (struct sockaddr *)&RSTSocket, sizeof(RSTSocket)) < 0)
    {
        printf("\n Connection failed \n");
        return false;
    }

    printf("\n Client connected to server!(RST)\n");
    return true;
}

class MotionAction
{
protected:

    ros::NodeHandle nh_;
    actionlib::SimpleActionServer<gogo_gazelle::MotionAction> as_;
    std::string action_name_;

    gogo_gazelle::MotionFeedback feedback_;
    gogo_gazelle::MotionResult result_;

public:
    MotionAction(std::string name) :
        as_(nh_, name, boost::bind(&MotionAction::executeCB, this, _1), false),
        action_name_(name)
    {
        as_.start();
    }

    ~MotionAction(void)
    {
    }

    void writeTX(const gogo_gazelle::MotionGoalConstPtr &goal)
    {
        //char *buffed = new char[TX.size];
        void *buffed;

        //send over the TX motion data
        TX.command.ros_cmd = goal->ros_cmd;

        TX.command.step_num = goal->step_num;
        TX.command.footstep_flag = goal->footstep_flag;
        TX.command.lr_state = goal->lr_state;

        for(int i=0;i<4;i++)
        {
            TX.command.des_footsteps[i].x = goal->des_footsteps[5*i];
            TX.command.des_footsteps[i].y = goal->des_footsteps[5*i + 1];
            TX.command.des_footsteps[i].r = goal->des_footsteps[5*i + 2];
            TX.command.des_footsteps[i].step_phase = goal->des_footsteps[5*i + 3];
            TX.command.des_footsteps[i].lr_state = goal->des_footsteps[5*i + 4];

            printf("step_phase = %d, lr_State = %d\n",TX.command.des_footsteps[i].step_phase, TX.command.des_footsteps[i].lr_state);
        }

        buffed = (void*)malloc(TX.size);
        memcpy(buffed, &TX.command, TX.size);

        send(sock_status, buffed, TX.size, 0);

        free(buffed);
        buffed = nullptr;

        return;
    }

    //only called when client requests goal
    void executeCB(const gogo_gazelle::MotionGoalConstPtr &goal)
    {
        bool success = true;

        //===write to PODO(Gazelle)===
        int RXDoneFlag = 0;
        int activeFlag = false;

        //write TX to PODO(Gazelle)
        writeTX(goal);

        //loop until TX complete

        int cnt = 0;
        while(RXDoneFlag == 0)
        {
            if(read(sock_result, RXResultBuffer, RXResultDataSize) == RXResultDataSize)
            {
                memcpy(&RXresult, RXResultBuffer, RXResultDataSize);
            }
            if(cnt > 2000000)
            {
                printf("gazelle result = %d\n",RXresult.gazelle_result);
                cnt = 0;
            }
            cnt ++;

            //check that preempt has not been requested by client
            if(as_.isPreemptRequested() || !ros::ok())
            {
                ROS_INFO("%s: Preempted", action_name_.c_str());
                as_.setPreempted();
                success = false;
                break;
            }

            //check result flag
            switch(RXresult.gazelle_result)
            {
            case CMD_ACCEPT:
                printf("gazelle accept command\n");
                RXDoneFlag = true;
                break;
            case CMD_ERROR:
                printf("gazelle error \n");
                success = false;
                RXDoneFlag = true;
                break;
            case CMD_DONE:
                ROS_INFO("ONE STEP DONE  %d-----------------------",RXresult.step_phase);
                RXDoneFlag = true;
                break;
            case CMD_WALKING_FINISHED:
                ROS_INFO("--------WALKING FINISHED----------");
                RXDoneFlag = true;
                break;
            }

            //result setting
            result_.gazelle_result = RXresult.gazelle_result;
            result_.step_phase = RXresult.step_phase;
            result_.lr_state = RXresult.lr_state;
        }


        if(success)
        {
            ROS_INFO("%d: Succeeded", RXresult.gazelle_result);
            as_.setSucceeded(result_);
        }else
        {
            ROS_INFO("%d: Aborted", RXresult.gazelle_result);
            as_.setAborted(result_);
        }
    }

    int returnServerStatus()
    {
        if(as_.isActive())
            return 1;
        else
            return 0;
    }
};


int main(int argc, char *argv[])
{
    ros::init(argc, argv, "gogo_gazelle");
    ros::NodeHandle n;

    robot_states_pub = n.advertise<gogo_gazelle::update>("/robot_states",1);

    if(connectRST() == false)
    {
        printf("waitForResult\n\n Failed to connect. Closing...\n");
        return -1;
    }
    if(connectROS() == false)
    {
        printf("waitForResult\n\n Failed to connect. Closing...\n");
        return -1;
    }

    ROS_INFO("Starting Action Server");
    MotionAction walking("walking");

    while(1)
    {
        //read robot status from PODO
        if(read(sock_status, RXBuffer, RXDataSize) == RXDataSize)
        {
            memcpy(&RX, RXBuffer, RXDataSize);
        }else
        {
			
		}
        //publish robot status
        //message.robot_state = RX.robot_state;
        message.step_phase = RX.step_phase;

        for(int i=0;i<3;i++)
        {
            message.pel_pos_est[i] = RX.pel_pos_est[i];
        }
        /*
        for(int i=0;i<31;i++)
        {
            //message.joint_reference[i] = RX.joint_reference[i];
            //message.joint_encoder[i] = RX.joint_encoder[i];
        }
        for(int i=0;i<12;i++)
        {
            message.ft_sensor[i] = RX.ft_sensor[i];
        }
        for(int i=0;i<9;i++)
        {
            message.imu_sensor[i] = RX.imu_sensor[i];
        }*/

        robot_states_pub.publish(message);
        //callback check
        ros::spinOnce();
    }

    return 0;
}
