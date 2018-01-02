#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>	// SetThreadPriority 
#include <thread>				// std::thread

using std::chrono::milliseconds;
using std::chrono::duration;
using std::thread;

class TimedEvent
{
protected:
	duration<long long, std::milli> IntervalTime;
	const unsigned char MaxTicks;
	unsigned char CurTicks;
	bool Abort;
	bool Running;
	thread TimedThread;

private:
	inline virtual void OnTick() = 0;
	inline virtual void OnExpire() = 0;

public:

	inline void StartTimer() { Running = true; }
	inline void StopTimer() { Running = false; }
	inline const bool TimerRunning() const { return Running; }

	//	TODO: Multiply LastRTT here by some small percentage
	//	Based on the variation between the last few values of LastRTT
	//	This will smooth out random hiccups in the network
	inline void NewInterval(const long long &LastRTT) { IntervalTime = milliseconds((const unsigned int)ceil(LastRTT)); }

	//	Constructor
	inline TimedEvent(milliseconds Interval, const unsigned char iMaxTicks) :
		IntervalTime(Interval), MaxTicks(iMaxTicks), CurTicks(0), Abort(false), Running(false),
		TimedThread([&]() {
		//
		//	Make sure this thread uses the least amount of resources possible
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

		while (!Abort)
		{
			if (Running)
			{
				if ((MaxTicks == 0 || CurTicks < MaxTicks))
				{
					OnTick();
				} else { OnExpire(); return; }
			}
			std::this_thread::sleep_for(IntervalTime);
		}	}) {}

	//	Destructor
	inline virtual ~TimedEvent() {
		Running = false;
		Abort = true;
		//	Would like to use .join() here instead
		//	However that throws an odd exception..
		TimedThread.detach();
	}
};