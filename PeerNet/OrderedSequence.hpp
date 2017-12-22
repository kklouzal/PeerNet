#pragma once
#include <unordered_map>

using std::unordered_map;

class OrderedSequence
{
	unordered_map<unsigned long, bool> MissingIDs;
	unordered_map<unsigned long, bool> IDs;
	unsigned int HighestID;
	unsigned int LowestID;

public:
	OrderedSequence() : HighestID(0), LowestID(0) {}

	inline void Update(const unsigned long &ID)
	{
		//	If this ID was missing, remove it from the MissingIDs container
		if (MissingIDs.count(ID)) { MissingIDs.erase(ID); }

		//	Ignore ID's below the LowestID
		if (ID <= LowestID) { return; }

		//	Update our HighestID if needed
		if (ID > HighestID) { HighestID = ID; }

		//	Update our LowestID if needed
		if (ID == LowestID + 1) {
			++LowestID;
			// Loop through our IDs container until we cant find (LowestID+1)
			while (IDs.count(LowestID + 1))
			{
				++LowestID;
				IDs.erase(LowestID);
			}
			return;
		}

		//	At this point ID must be greater than LowestID
		//	Which means we have an out-of-sequence ID
		IDs[ID] = true;
		for (unsigned long i = ID - 1; i > LowestID; --i)
		{
			if (!IDs.count(i)) { MissingIDs[i] = true; }
		}
	}

	inline const unordered_map<unsigned long, bool> &GetMissingIDs() const { return MissingIDs; }
};