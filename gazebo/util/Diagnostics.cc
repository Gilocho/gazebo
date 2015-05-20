/*
 * Copyright (C) 2012-2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifdef _WIN32
  #include <algorithm>
  // Ensure that Winsock2.h is included before Windows.h, which can get
  // pulled in by anybody (e.g., Boost).
  #include <Winsock2.h>
#endif

#include <iomanip>
#include "gazebo/common/Assert.hh"
#include "gazebo/common/CommonIface.hh"
#include "gazebo/common/Events.hh"
#include "gazebo/common/SystemPaths.hh"
#include "gazebo/math/SignalStats.hh"
#include "gazebo/transport/transport.hh"
#include "gazebo/util/Diagnostics.hh"

using namespace gazebo;
using namespace util;

//////////////////////////////////////////////////
DiagnosticManager::DiagnosticManager()
{
#ifndef _WIN32
  const char *homePath = common::getEnv("HOME");
#else
  const char *homePath = common::getEnv("HOMEPATH");
#endif

  // Get the base of the time logging path
  if (!homePath)
  {
    common::SystemPaths *paths = common::SystemPaths::Instance();
    gzwarn << "HOME environment variable missing. Diagnostic timing " <<
      "information will be logged to " << paths->GetTmpPath() << "\n";
    this->logPath = paths->GetTmpPath();
    this->logPath /= "gazebo";
  }
  else
  {
    this->logPath = homePath;
    this->logPath /= ".gazebo";
  }

  std::string timeStr = common::Time::GetWallTimeAsISOString();

#ifdef _WIN32
  std::replace(timeStr.begin(), timeStr.end(), ':', '_');
#endif

  this->logPath /= "diagnostics";
  this->logPath /= timeStr;

  // Make sure the path exists.
  if (!boost::filesystem::exists(this->logPath))
    boost::filesystem::create_directories(this->logPath);
}

//////////////////////////////////////////////////
DiagnosticManager::~DiagnosticManager()
{
}

//////////////////////////////////////////////////
void DiagnosticManager::Init(const std::string &_worldName)
{
  this->node.reset(new transport::Node());

  this->node->Init(_worldName);

  this->pub = this->node->Advertise<msgs::Diagnostics>("~/diagnostics");

  this->updateConnection = event::Events::ConnectWorldUpdateBegin(
      boost::bind(&DiagnosticManager::Update, this, _1));
}

//////////////////////////////////////////////////
void DiagnosticManager::Fini()
{
  event::Events::DisconnectWorldUpdateBegin(this->updateConnection);

  this->timers.clear();
}

//////////////////////////////////////////////////
boost::filesystem::path DiagnosticManager::GetLogPath() const
{
  return this->logPath;
}

//////////////////////////////////////////////////
void DiagnosticManager::Update(const common::UpdateInfo &_info)
{
  if (_info.realTime > common::Time::Zero)
    this->msg.set_real_time_factor((_info.simTime / _info.realTime).Double());
  else
    this->msg.set_real_time_factor(0.0);

  msgs::Set(this->msg.mutable_real_time(), _info.realTime);
  msgs::Set(this->msg.mutable_sim_time(), _info.simTime);

  if (this->pub && this->pub->HasConnections())
    this->pub->Publish(this->msg);

  this->msg.clear_time();
}

//////////////////////////////////////////////////
void DiagnosticManager::AddTime(const std::string &_name,
    common::Time &_wallTime, common::Time &_elapsedTime)
{
  msgs::Diagnostics::DiagTime *time = this->msg.add_time();
  time->set_name(_name);
  msgs::Set(time->mutable_elapsed(), _elapsedTime);
  msgs::Set(time->mutable_wall(), _wallTime);
}

//////////////////////////////////////////////////
void DiagnosticManager::StartTimer(const std::string &_name)
{
  TimerMap::iterator iter = this->timers.find(_name);
  if (iter != this->timers.end())
  {
    GZ_ASSERT(iter->second != NULL, "DiagnosticTimerPtr is NULL");
    iter->second->Start();
  }
  else
  {
    this->timers[_name] = DiagnosticTimerPtr(new DiagnosticTimer(_name));
  }
}

//////////////////////////////////////////////////
void DiagnosticManager::StopTimer(const std::string &_name)
{
  TimerMap::iterator iter = this->timers.find(_name);
  if (iter != this->timers.end())
  {
    GZ_ASSERT(iter->second, "DiagnosticTimerPtr is NULL");
    iter->second->Stop();
  }
  else
    gzerr << "Unable to find timer[" << _name << "]\n";
}

//////////////////////////////////////////////////
void DiagnosticManager::Lap(const std::string &_name,
                            const std::string &_prefix)
{
  TimerMap::iterator iter;
  iter = this->timers.find(_name);

  if (iter == this->timers.end())
    gzerr << "Unable to find timer with name[" << _name << "]\n";
  else
  {
    GZ_ASSERT(iter->second, "DiagnosticTimerPtr is NULL");

    iter->second->Lap(_prefix);
  }
}

//////////////////////////////////////////////////
int DiagnosticManager::GetTimerCount() const
{
  return this->timers.size();
}

//////////////////////////////////////////////////
common::Time DiagnosticManager::GetTime(int _index) const
{
  if (_index < 0 || static_cast<size_t>(_index) > this->timers.size())
  {
    gzerr << "Invalid index of[" << _index << "]. Must be between 0 and "
      << this->timers.size()-1 << ", inclusive.\n";
    return common::Time();
  }

  TimerMap::const_iterator iter;

  iter = this->timers.begin();
  std::advance(iter, _index);

  if (iter != this->timers.end())
  {
    GZ_ASSERT(iter->second, "DiagnosticTimerPtr is NULL");
    return iter->second->GetElapsed();
  }
  else
    gzerr << "Error getting time\n";

  return common::Time();
}

//////////////////////////////////////////////////
std::string DiagnosticManager::GetLabel(int _index) const
{
  if (_index < 0 || static_cast<size_t>(_index) > this->timers.size())
  {
    gzerr << "Invalid index of[" << _index << "]. Must be between 0 and "
      << this->timers.size()-1 << ", inclusive.\n";
    return std::string();
  }
  TimerMap::const_iterator iter;

  iter = this->timers.begin();
  std::advance(iter, _index);

  if (iter != this->timers.end())
    return iter->first;
  else
    gzerr << "Erorr getting label\n";

  return "null";
}

//////////////////////////////////////////////////
common::Time DiagnosticManager::GetTime(const std::string &_label) const
{
  TimerMap::const_iterator iter;
  iter = this->timers.find(_label);

  if (iter != this->timers.end())
  {
    GZ_ASSERT(iter->second, "DiagnosticTimerPtr is NULL");
    return iter->second->GetElapsed();
  }
  else
    gzerr << "Error getting time\n";

  return common::Time();
}

//////////////////////////////////////////////////
DiagnosticTimer::DiagnosticTimer(const std::string &_name) : Timer()
{
  boost::filesystem::path logPath;

  logPath = DiagnosticManager::Instance()->GetLogPath() / (_name + ".log");
  this->log.open(logPath.string().c_str(), std::ios::out | std::ios::app);

  this->name = _name;
  this->Start();
}

//////////////////////////////////////////////////
DiagnosticTimer::~DiagnosticTimer()
{
  this->Stop();
  for (auto const &measurement: this->stats)
  {
    std::ostringstream scopedName;
    if (measurement.first != this->name)
      scopedName << this->name << "::";
    scopedName << measurement.first;

    this->log << std::setw(50) << std::left << scopedName.str();
    for (auto const &stat: measurement.second.Map())
    {
      this->log << std::setw(7) << stat.first
                << std::setw(15) << std::scientific << stat.second;
    }
    this->log << std::endl;
  }
  this->log.flush();
  this->log.close();
}

//////////////////////////////////////////////////
void DiagnosticTimer::Start()
{
  // Only start if not running.
  if (!this->GetRunning())
  {
    // Make sure the previous lap is reset
    this->prevLap = this->GetElapsed();

    // Start the timer
    Timer::Start();
  }
}

//////////////////////////////////////////////////
void DiagnosticTimer::Stop()
{
  // Only stop if currently running
  if (this->GetRunning())
  {
    // Stop the timer
    Timer::Stop();

    common::Time elapsed = this->GetElapsed();
    common::Time currTime = common::Time::GetWallTime();
    this->cumulativeTime += elapsed;

    // Record the total elapsed time.
    this->InsertData(this->name, elapsed.Double());

    DiagnosticManager::Instance()->AddTime(this->name, currTime, elapsed);

    // Reset the lap time and timer
    this->prevLap.Set(0, 0);
    this->Reset();
  }
}

//////////////////////////////////////////////////
void DiagnosticTimer::Lap(const std::string &_prefix)
{
  // Get the current elapsed time.
  common::Time elapsed = this->GetElapsed();
  common::Time delta = elapsed - this->prevLap;
  common::Time currTime = common::Time::GetWallTime();

  // Record the delta time.
  this->InsertData(_prefix, elapsed.Double());

  DiagnosticManager::Instance()->AddTime(this->name + ":" + _prefix,
      currTime, delta);

  // Store the previous lap time.
  this->prevLap = elapsed;
}

//////////////////////////////////////////////////
void DiagnosticTimer::InsertData(const std::string &_name,
                                 const common::Time &_time)
{
  auto iter = this->stats.find(_name);
  if (iter == this->stats.end())
  {
    this->stats[_name] = math::SignalStats();
    iter = this->stats.find(_name);
    iter->second.InsertStatistics("mean,maxAbs,var");
  }
  iter->second.InsertData(_time.Double());
}

