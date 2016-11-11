#pragma once
#include <thread>			// std::thread

using std::thread;

class TimedEvent
{
protected:
	const std::chrono::milliseconds IntervalTime;
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

	//	Constructor
	TimedEvent(std::chrono::milliseconds Interval, const unsigned char iMaxTicks) :
		IntervalTime(Interval), MaxTicks(iMaxTicks), CurTicks(0), Abort(false), Running(false),
		TimedThread([&]() {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

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
			printf("\tDestroy Timed Event\n");
			Abort = true;
			TimedThread.join();
			printf("\tTimed Event Destroyed\n");
		}
};