#pragma once
#include <thread>			// std::thread
#include <functional>		// std::function

using std::thread;
using std::function;

class TimedEvent
{
	const function<void()> OnTick;
	const function<void()> OnExpire;
	const std::chrono::milliseconds IntervalTime;
	const unsigned char MaxTicks;
	unsigned char CurTicks;
	bool Running;
	thread TimedThread;

public:

	//	Constructor
	TimedEvent(const function<void()> OnTickFunc, const function<void()> OnExpireFunc, std::chrono::milliseconds Interval, const unsigned char iMaxTicks) :
		OnTick(OnTickFunc), OnExpire(OnExpireFunc), IntervalTime(Interval), MaxTicks(iMaxTicks), CurTicks(0), Running(true),
		TimedThread([&]() {
		SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
		while (CurTicks < MaxTicks)
		{
			if (!Running) { return; }
			CurTicks++;
			OnTick();
			std::this_thread::sleep_for(IntervalTime);
		} OnExpire(); }) {}

		//	Destructor
		~TimedEvent() {
			Running = false;
			TimedThread.join();
		}
};