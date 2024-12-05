
#include "Test.h"

int main()
{
	if (!Test::TestPutGet())
	{
		printf("Put/Get test failed\n");
	}
	else
	{
		printf("Put/Get test passed\n");
	}
	if (!Test::TestBadPersistedFileShouldThrowException())
	{
		printf("Bad persisted file test failed\n");
	}
	else
	{
		printf("Bad persisted file test passed\n");
	}
	if(!Test::TestLoadData())
	{
		printf("Load data test failed\n");
	}
	else
	{
		printf("Load data test passed\n");
	}
	if (!Test::TestReloadData())
	{
		printf("Reload data test failed\n");
	}
	else
	{
		printf("Reload data test passed\n");
	}
	if(!Test::StressTestReadWrite(false))
	{
		printf("Write-heavy stress test failed\n");
	}
	else
	{
		printf("Write-heavy stress test passed\n");
	}
	if (!Test::StressTestReadWrite(true))
	{
		printf("Stress test failed\n");
	}
	else
	{
		printf("Stress test passed\n");
	}
	// while this looks like the general write-heavy stress test, because everything is written to the same key only one queue collects writes
	// (rather than 1024), AND there is more lock contention, so the queue footprint is much smaller.  The test is therefore retained, even though
	// the scenario is not strictly speaking one we care about.
	if (!Test::StressTestReadWriteWorstCase(false))
	{
		printf("Worst-case write-heavy stress test failed\n");
	}
	else
	{
		printf("Worst-case write-heavy stress test passed\n");
	}
	if (!Test::StressTestReadWriteWorstCase(true))
	{
		printf("Worst-case stress test failed\n");
	}
	else
	{
		printf("Worst-case stress test passed\n");
	}
}