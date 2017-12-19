#pragma once
#include <Processthreadsapi.h>	// SetThreadPriority 
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
	virtual void OnTick() = 0;
	virtual void OnExpire() = 0;

public:

	void StartTimer() { Running = true; }
	void StopTimer() { Running = false; }
	const bool TimerRunning() const { return Running; }

	//	TODO: Multiply LastRTT here by some small percentage
	//	Based on the variation between the last few values of LastRTT
	//	This will smooth out random hiccups in the network
	void NewInterval(duration<double, std::milli> LastRTT) { IntervalTime = milliseconds((const unsigned int)ceil(LastRTT.count())); }

	//	Constructor
	TimedEvent(milliseconds Interval, const unsigned char iMaxTicks) :
		IntervalTime(Interval), MaxTicks(iMaxTicks), CurTicks(0), Abort(false), Running(false),
		TimedThread([&]() {
		//
		//	Make sure this thread uses the least amount of resources possible
		SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

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
		}}) {}

		//	Destructor
		~TimedEvent() {
			Abort = true;
			TimedThread.join();
		}
};