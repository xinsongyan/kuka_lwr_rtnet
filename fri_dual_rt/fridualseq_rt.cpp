/*********************************************************************
 *
 *  Copyright (c) 2012, Jeannette Bohg - MPI for Intelligent Systems
 *  (jbohg@tuebingen.mpg.de)
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Jeannette Bohg nor the names of MPI
 *     may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

 /********************************************************************
   fridualseq_rt.cpp
   Based on KUKA version of FRI examples.
   This is an educational example of how the communication to two arms 
   will *NOT* work. It is derived from the simple example fritest_rt.cpp
   that copies back whatever it receives into command structure. 
   It only spawns one realtime thread from which it controls the arms 
   sequentially. In this way, it won't be possible to respond to any of
   the two arms in the required 1ms. This could be possible if the arms 
   very perfectly synchronised. But there is no possibility to do that. 
   Each arm runs its own realtime clock and the remote xenomai machine
   runs another realtime clock.
   The example fridual_rt.cpp shows how you can communicate with both arms 
   simultaneoulsy but asynchronously by spawning one thread per arm.
 *******************************************************************/


#include <native/task.h>
#include <native/pipe.h>
#include <native/timer.h>
#include <sys/mman.h>

#include <iostream>
#include <cstdlib>
#include <math.h>
#include <limits.h>
#include <fcntl.h>
#include <boost/thread.hpp>

#include "friudp_rt.h"
#include "friremote_rt.h"

#ifndef M_PI 
#define M_PI 3.14159
#endif


using namespace std;

bool going = true;

double T_s = 1.0/1000.0;

RT_TASK dual_task;

static const string ip_left = "192.168.0.20";
static const string ip_right = "192.168.1.20";


void waitForEnter()
{
  std::string line;
  std::getline(std::cin, line);
  std::cout << line << std::endl;
}

void warnOnSwitchToSecondaryMode(int)
{
  std::cerr << "WARNING: Switched out of RealTime. Stack-trace in syslog.\n";
}

void dualControlLoop(void* cookie)
{
  signal(SIGXCPU, warnOnSwitchToSecondaryMode);
  rt_task_set_periodic(NULL, TM_NOW, T_s * 1e9);
  rt_task_set_mode(0, T_WARNSW, NULL);  

  rt_task_wait_period(NULL);

  friRemote friInst_l(49938, ip_left.c_str());
  friRemote friInst_r(49938, ip_right.c_str());
  
  FRI_QUALITY lastQuality_l = FRI_QUALITY_BAD;
  FRI_QUALITY lastQuality_r = FRI_QUALITY_BAD;

  int res_l = -1;
  int res_r = -1;

  int ret;
  unsigned long overrun;

  while(going)
    {
      // wait for next periodic release point 
      ret = rt_task_wait_period(&overrun);
      if(ret)
	printf("error \n");
      if (ret == -EWOULDBLOCK) {
	printf("EWOULBLOCK while rt_task_wait_period, code %d\n",ret);
	//return;
      } else if(ret == -EINTR){
	printf("EINT while rt_task_wait_period, code %d\n",ret);
      } else if(ret == -ETIMEDOUT){
	printf("ETIMEDOUT while rt_task_wait_period, code %d\n",ret);
      } else if(ret == -EPERM){
	printf("EPERM while rt_task_wait_period, code %d\n",ret);
      }
      

      res_l = friInst_l.doReceiveData();
      //if(res<0) 
      //	continue;
      
      res_r = friInst_r.doReceiveData();

      /// perform some arbitrary handshake to KRL -- possible in monitor mode already
      // send to krl int a value
      // handshake with left arm
      friInst_l.setToKRLInt(0,1);
      lastQuality_l = friInst_l.getQuality();
      if ( lastQuality_l >= FRI_QUALITY_OK)
	{
	  // send a second marker
	  friInst_l.setToKRLInt(0,10);
	}
      //
      // just mirror the real value..
      //
      friInst_l.setToKRLReal(0,friInst_l.getFrmKRLReal(1));

      
      // handshake with right arm
      friInst_r.setToKRLInt(0,1);
      lastQuality_r = friInst_r.getQuality();
      if ( lastQuality_r >= FRI_QUALITY_OK)
	{
	  // send a second marker
	  friInst_r.setToKRLInt(0,10);
	}
      //
      // just mirror the real value..
      //
      friInst_r.setToKRLReal(0,friInst_r.getFrmKRLReal(1));
      
      // Mirror old joint values 
      friInst_l.doTest();
      friInst_r.doTest();
      
      // Send packages if packages have been received in this cycle
      if(res_l==0)
	friInst_l.doSendData();

      if(res_r==0)
	friInst_r.doSendData();

    }


}

int main (int argc, char *argv[])
{
  std::string ans;
  //  int tmp = 0;

  cout << "Opening FRI Version " 
       << FRI_MAJOR_VERSION << "." << FRI_SUB_VERSION 
       << "." <<FRI_DATAGRAM_ID_CMD << "." <<FRI_DATAGRAM_ID_MSR 
       << " Interface for Communication Test" << endl;
  {
    // do checks, whether the interface - and the host meets the requirements
    // Note:: This Check remains in friRemote.cpp -- should go to your code ...
    FRI_PREPARE_CHECK_BYTE_ORDER;
    if (!FRI_CHECK_BYTE_ORDER_OK) 
      {
	cerr << "Byte order on your system is not appropriate - expect deep trouble" <<endl;
      }
    if (!FRI_CHECK_SIZES_OK)
      {
	cout << "Sizes of datastructures not appropriate - expect even deeper trouble" << endl;
	      
      }
  }

  mlockall(MCL_CURRENT | MCL_FUTURE);
  rt_task_shadow(NULL, "fri_dual_seq_rt", 50, 0);
  
  rt_task_create(&dual_task, "Dual loop", 0, 50, T_JOINABLE | T_FPU);
  rt_task_start(&dual_task, &dualControlLoop, NULL);
  rt_task_sleep(1e6);

  std::cout << "Press [Enter] to exit.\n";
  waitForEnter();
  std::cout << "exiting\n";
  
  going = false;
  //  rt_task_join(&left_task);
  //  rt_task_join(&right_task);
  
  rt_task_join(&dual_task);
  
  return EXIT_SUCCESS;
}
/* @} */
