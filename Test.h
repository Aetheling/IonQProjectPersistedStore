#pragma once
#include "PersistedStore.h"

class Test
{
public:
	// functional tests
	static bool TestPutGet();
	static bool TestLoadData();
	static bool TestReloadData();
	static bool TestBadPersistedFileShouldThrowException();

	// stress test the store.  64 threads doing reads and writes to 1024 different keys.
	// The store has 512 lock partitions, so key contention shouldn't be too horrendous.
	// Shouldn't crash.
	// If singleWriter is set, only ONE thread writes; otherwise, half do.
	static bool StressTestReadWrite(bool singleWriter);
	// perf/stress test -- shouldn't crash; should give performance numbers
	// if heavy read, 99% of requests are read requests; otherwise, half are
	// Worst-case: all reads/writes to the same key, so every thread is using the same
	// lock.
	// If singleWriter is set, only ONE thread writes; otherwise, half do.
	static bool StressTestReadWriteWorstCase(bool singleWriter);
private:
	static void CleanDirectoryForTest();
};
