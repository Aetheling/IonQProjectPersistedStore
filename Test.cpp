#include "Test.h"
#include "PersistedStore.cpp" // simplest methof to include template class
#include "Constants.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <stdio.h>

// use the same key pretty much throughout
string key("key");
string baseKeyValue("value of key ");

bool Test::TestLoadData()
{
	char buffer[64];
	// start from a clean slate
	remove("test_data_a");
	// open the file for writing
	ofstream persisted("test_data_a");
	// write 3 values to the file test_data_a: two valid and an incomplete,  Both valid keys should be available, but not the third
	// key 0: full:
	((unsigned int*) buffer)[0] = 1;
	((unsigned int*) buffer)[1] = 11;
	for(int i=0;i<12;i++) buffer[2*sizeof(unsigned int)+i] = '0'+i;
	persisted.write(buffer,2*sizeof(unsigned int) + 12); // size of the two sizes, + 1 for the key size, + 11 for the data size
	// key 1: full
	((unsigned int*) buffer)[0] = 1;
	((unsigned int*) buffer)[1] = 11;
	for(int i=0;i<12;i++) buffer[2*sizeof(unsigned int)+i] = '0'+1+i;
	persisted.write(buffer,2*sizeof(unsigned int) + 12);
	// key 2: incomplete
	((unsigned int*) buffer)[0] = 1;
	((unsigned int*) buffer)[1] = 11;
	for(int i=0;i<12;i++) buffer[2*sizeof(unsigned int)+i] = '0'+2+i;
	persisted.write(buffer,2*sizeof(unsigned int) + 11); // + 11: NOT the full size expected!
	persisted.flush();
	persisted.close();
	PersistedStore<Value, 6> store(EReverseString, ENumComressionTypes, true, "test_data_a");
	// should be able to read it again...  after a pause to make sure the write gets through
	this_thread::sleep_for(chrono::milliseconds(100));
	string key = "0";
	shared_ptr<Value> found = store.get(key);
	if (nullptr == found)
	{
		printf("couldn't find key 0\n");
		return false;
	}
	if (found->m_size != 11)
	{
		printf("Key 0 found value has wrong size\n");
		return false;
	}
	for (int i=0; i<11; i++)
	{
		if (found->m_data[i] != '0' + 1 + i)
		{
			printf("Key 0 has bad data\n");
			return false;
		}
	}
	key = "1";
	found = store.get(key);
	if (nullptr == found)
	{
		printf("couldn't find key 1\n");
		return false;
	}
	if (found->m_size != 11)
	{
		printf("Key 1 found value has wrong size\n");
		return false;
	}
	for (int i = 0; i < 11; i++)
	{
		if (found->m_data[i] != '0' + 2 + i)
		{
			printf("Key 1 has bad data\n");
			return false;
		}
	}
	key = "2";
	found = store.get(key);
	if (nullptr != found)
	{
		printf("Key 2 shouldn't be there at all, but it was\n");
		return false;
	}
	return true;
}

bool Test::TestPutGet()
{
	const char* keyval = "value of key";
	try
	{
		char* value = (char*) malloc(sizeof(char)*12);
		memcpy_s(value, 12*sizeof(char), keyval, 12*sizeof(char));
		CleanDirectoryForTest();
		PersistedStore<Value, 6> store(EReverseString, ENumComressionTypes, true);
		// write a value to the store
		Value v(12, value);
		store.put(key, v);
		// should be able to read it again...  after a pause to make sure the write gets through
		this_thread::sleep_for(chrono::milliseconds(100));
		shared_ptr<Value> found = store.get(key);
		if (nullptr == found)
		{
			printf("Couldn't find data just written\n");
			return false;
		}
		if (*found != v)
		{
			printf("Found value not what was written\n");
			return false;
		}
	}
	catch (exception e)
	{
		printf("Put/Get test failed with exception: \n", e.what());
		return false;
	}
	return true;
}

bool Test::TestReloadData()
{
	try
	{
		CleanDirectoryForTest();
		for (int i=0; i<10; i++)
		{
			string newKeyValue = baseKeyValue;
			newKeyValue.append(to_string(i));
			char* buffer = (char*) malloc(sizeof(char)*newKeyValue.length());
			for (int j=0; j<newKeyValue.length(); j++)
			{
				buffer[j] = newKeyValue[j];
			}
			Value newValue(newKeyValue.length(), buffer);
			// mis up the compression used with each iteration
			PersistedStore<Value, 6> store((0==i%2)      ? EReverseString : ENumComressionTypes,
				                           (0==(i>>1)%2) ? EReverseString : ENumComressionTypes,
				                           false);
			if (0 != i)
			{
				// store should have data from previous iteration
				string expectedValue = baseKeyValue;
				expectedValue.append(to_string(i - 1));
				shared_ptr<Value> found = store.get(key);
				if (nullptr == found)
				{
					printf("Couldn't find data\n");
					return false;
				}
				else if (expectedValue.length() != found->m_size)
				{
					printf("Found data has unexpected size\n");
					return false;
				}
				for (int j=0; j<expectedValue.length(); j++)
				{
					if (found->m_data[j] != expectedValue[j])
					{
						printf("Data found not what was expected\n");
						return false;
					}
				}
			}
			// write the new value
			store.put(key, newValue);
			// just in case it takes some time for the store to persist the data to file
			this_thread::sleep_for(chrono::milliseconds(100));
		}
	}
	catch (exception e)
	{
		printf("Reload data test failed with exception: %s\n", e.what());
		return false;
	}
	return true;
}

bool Test::TestBadPersistedFileShouldThrowException()
{
	try
	{
		PersistedStore<Value, 6> store(ENumComressionTypes, ENumComressionTypes, true, "StoreThatJustDoesNotExist.txt");
		return false; // shouldn't work
	}
	catch (exception e)
	{
		return true;
	}
}

// globals for multithreaded stress tests
const unsigned int RequestsToMake = 10000;
bool threadHadError;
bool readHeavy;
int unfoundData = 0; // counts times the read fails to find a value in the stress tests.  Purely to make sure compiler doesn't optimize away the reads

// "worst case" refers to locki contention, NOT data size.
void ReadWriteDataThreadWorstCase(PersistedStore <Value, 6> *store, bool writeThread)
{
	// lots of threads all reading and writing to the same key.  Sure to cause collisions; shouldn't cause issues
	// Note that since we don't synchronize with the other threads, we don't know WHAT value to expect.  Except at the
	// very end, when all threads write their final values
	try
	{
		for(int i=0; i<RequestsToMake; i++)
		{
			int  valueSize = (1 + i%100)*5000; // key value between 5k and 500k
			char *buffer   = (char *) malloc(sizeof(char)*valueSize);
			for(int j=0; j<valueSize; j++)
			{
				buffer[j] = 'a';
			}
			Value newValue(valueSize, buffer); // NOTE: newValue owns buffer now; we do NOT need to free it
			if (writeThread)
			{
				// write
				store->put(key, newValue);
			}
			else
			{
				// read
				shared_ptr<Value> found = store->get(key);
				if (nullptr == found)
				{
					// Writes go to file on another thread before they are available in the store; could have a thread try to read that key
					// before the write thread picks up the request and deals with it.
					// We don't really care about the missed count; this is purely to make sure the read is not optimized away by the compiler.
					unfoundData++;
				}
			}
		}
	}
	catch (exception e)
	{
		printf("Exception thrown : %s\n", e.what());
		threadHadError = true;
	}
}

bool Test::StressTestReadWriteWorstCase(bool singleWriter)
{
	threadHadError = false;
#if _DEBUG
	printf("You seem to be running debug bits for StressTestReadWriteWorstCase.  You'll get better perf numbers with a retail build.\n");
#endif
	printf("This is a worst-case scenario -- all threads are writing to the same key\n");
	try
	{
		CleanDirectoryForTest();
		const unsigned int numWorkers = 64;
		// write, read equal: last write request was for ((RequestsToMake-1)/2)*2
		string lastWrittenValue;
		// the last written value was a string of (1 + (RequestToMake-1)%100)*5000 copies of the chareacter 'a'
		for(int i=0; i<(1 + (RequestsToMake-1)%100)*5000; i++)
		{
			lastWrittenValue.append("a");
		}
		std::chrono::time_point<std::chrono::system_clock> start, end;
		std::chrono::duration<double> elapsed_seconds;
		thread* workers[numWorkers];
		PersistedStore<Value, 6> store(EReverseString, ENumComressionTypes, singleWriter);
		// start a timer -- see how fast we can get through all the requests
		start = std::chrono::system_clock::now();
		// fire up worker threads
		for (int i=0; i<numWorkers; i++)
		{
			bool writeThread = (singleWriter) ? (0==i) : (i<34);
			workers[i] = new thread(ReadWriteDataThreadWorstCase, &store, writeThread);
		}
		// Wait for all workers to complete
		for (int i = 0; i < numWorkers; i++)
		{
			workers[i]->join();
		}
		// check how long everything took
		end = std::chrono::system_clock::now();
		elapsed_seconds = end - start;
		printf("It took %lf seconds for %i threads to perform %i put/get operations each -- all to the same key.  %i threads sent writes\n", elapsed_seconds.count(), numWorkers, RequestsToMake, singleWriter ? 1 : 34);
		// check store contains expected value -- after a short pause to make sure the last few writes got through
		this_thread::sleep_for(chrono::milliseconds(1000));
		shared_ptr<Value> found = store.get(key);
		if (nullptr == found)
		{
			printf("Couldn't find data\n");
			return false;
		}
		if (lastWrittenValue.length() != found->m_size)
		{
			printf("Found value's size unexpected\n");
			return false;
		}
		if (0 != memcmp(found->m_data, lastWrittenValue.c_str(), found->m_size))
		{
			printf("Found value unexpected\n");
			return false;
		}
	}
	catch (exception e)
	{
		printf("stress test failed with exception %s\n", e.what());
		return false;
	}
	return !threadHadError;
}

void ReadWriteDataThread(PersistedStore <Value, 512> *store, bool writeThread)
{
	// lots of threads all reading and writing to the same key.  Sure to cause collisions; shouldn't cause issues
	// Note that since we don't synchronize with the other threads, we don't know WHAT value to expect.  Except at the
	// very end, when all threads write their final values
	try
	{
		for(int i=0; i<RequestsToMake; i++)
		{
			string newKey = key;
			int valueSize = (1 + i%100)*5000; // key value between 5k and 500k
			newKey.append(to_string(i%1024));
			char *buffer = (char *) malloc(sizeof(char)*valueSize);
			for(int j=0; j<valueSize; j++)
			{
				buffer[j] = 'a';
			}
			Value newValue(valueSize, buffer); // NOTE: Value owns buffer now; we do NOT need to free it
			// do more writes if not ready-heavy
			int writeFrequency = (readHeavy) ? 100 : 2;
			if (writeThread)
			{
				// write
				store->put(newKey, newValue);
			}
			else
			{
				// read
				shared_ptr<Value> found = store->get(newKey);
				if (nullptr == found)
				{
					// Writes go to file on another thread before they are available in the store; could have a thread try to read that key
					// before the write thread picks up the request and deals with it.
					// We don't really care about the missed count; this is purely to make sure the read is not optimized away by the compiler.
					unfoundData++;
				}
			}
		}
	}
	catch (exception e)
	{
		printf("Exception thrown : %s\n", e.what());
		threadHadError = true;
	}
}

bool Test::StressTestReadWrite(bool singleWriter)
{
	threadHadError = false;
#if _DEBUG
	printf("You seem to be running debug bits for StressTestReadWrite.  You'll get better perf numbers with a retail build.\n");
#endif
	try
	{
		CleanDirectoryForTest();
		const unsigned int numWorkers = 64;
		std::chrono::time_point<std::chrono::system_clock> start, end;
		std::chrono::duration<double> elapsed_seconds;
		thread* workers[numWorkers];
		PersistedStore<Value, 512> store(EReverseString, ENumComressionTypes, singleWriter);
		// start a timer -- see how fast we can get through all the requests
		start = std::chrono::system_clock::now();
		// fire up worker threads
		for (int i=0; i<numWorkers; i++)
		{
			bool bWriter = (singleWriter) ? (0==i) : (i<34); // either single writer, or slightly more than half
			workers[i] = new thread(ReadWriteDataThread, &store, bWriter);
		}
		// Wait for all workers to complete
		for (int i=0; i<numWorkers; i++)
		{
			workers[i]->join();
		}
		// check how long everything took
		end = std::chrono::system_clock::now();
		elapsed_seconds = end - start;
		printf("It took %lf seconds for %i threads to perform %i put/get operations each; %i threads sent writes\n", elapsed_seconds.count(), numWorkers, RequestsToMake, singleWriter ? 1 : 34);
		// check the results
		for(int i=RequestsToMake-1024; i<RequestsToMake; i++)
		{
			int valueSize = (1 + i%100)*5000;
		    char* buffer = (char*) malloc(sizeof(char)*valueSize);
			string newKey = key;
			newKey.append(to_string(i%1024));
			for(int j=0;j<valueSize;j++) buffer[j] = 'a';
			Value v(valueSize, buffer); // v owns buffer now
			auto f = store.get(newKey);
			if (*f != v)
			{
				printf("expected value not found for key %s\n", newKey.c_str());
				return false;
			}
		}
	}
	catch (exception e)
	{
		printf("stress test failed with exception %s\n", e.what());
		return false;
	}
	return !threadHadError;
}

#include <iostream>
#include <fstream>
#include <sys/stat.h>
void Test::CleanDirectoryForTest()
{
	unsigned int i = 1;
	string fileName;
	FILE* f;
	do
	{
		fileName = PersistedStoreName;
		fileName.append(to_string(i++));
		if (0 == fopen_s(&f, fileName.c_str(), "r"))
		{
			// file exists; get rid of it
			fclose(f);
			remove(fileName.c_str());
		}
		else
		{
			break;
		}
	} while (true);
}