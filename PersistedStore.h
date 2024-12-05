#pragma once
#include "EnumeratedTypes.h"
#include <unordered_map>
#include "Value.h"
#include <shared_mutex>
#include "Constants.h"

typedef shared_mutex Lock;

// DataType must have 2 methods:
// Serializer:
// void Serialize(char*& serializedValue, unsinged int &serializedSize): creates a character array which can be converted back to a DataType.  serializedValue belongs
// to caller on extt, who is responsible for deallocating it when needed.  Should be allocated with malloc
// 
// constructor:
// Datatype(char* serializedValue, unsigned int serializedSize): constructor taking the serialized value as arguments; rebuilds the original DataType object
// serializedValue remains owned by the caller, NOT the DataType created.  In particvular it may go out of scope
// if singleWriteThread is set, it is assumed that only ONE thread writes.  This makes locking simpler.
// In test, the write thread ONLY writes.  But it could do both reads and writes.  It only matters that the read threads never write.
template <class DataType, unsigned int numLocks>
class PersistedStore
{
public:
	// creates a persisted store, using the starting data from the latest version, UNLESS told to use the specific file given
	// in the optional persistedData variable.  Note that all stored versions will have version # of 1+
	// keyCompression and valueCompression could be template arguments insteasd of constructor arguments; the latter makes test simpler.
	PersistedStore(ECompressionTypes keyCompression, ECompressionTypes valueCompression, bool singleWriteThread, const char* persistedData = nullptr);
	~PersistedStore();
	// return nullptr if data not found
	shared_ptr<DataType> get(string& key);
	void put(string& key, DataType &v);

protected:
	// if s is purely a positive integer, returns its value; otherwise, returns -1.  (Implicit: that integer fits in an int).  Used in finding
	// latest persisted version of the store on file
	int GetIntegerValueFromString(const char* s);
	// assumes we have the lock, or the lock isn't needed (say, on startup)
	// key is assumed to be a null-terminated string.  Values assumed to be compressed already; JUST writes to the store -- not the file
	//void WriteValue(unsigned int keySize, const char* key, unsigned int valSize, const char* value, unsigned int lockPartition);
	// given the key, figures out which lock (and hence which persisted store) it belongs to.  ssumes null-terminated string
	unsigned int GetLockID(string& key);
	// compresses the data, according to the store algorithm.  No compression means a copy.
	// Returns the compressed size.  compressingKey flags whether compressing the key or the data -- might have different algorithms.
	// NOTE: in general, need to be careful about that output buffer -- don't know how big the compressed data will be.  It *might*
	// even be bigger than the uncompressed data!
	// Whatever compression algorithm is used for the key should result in data that is convertable to a string.  Needed for the store.
	// In particular, it should always end with a '\0' character.  Note that compression/decompression allocate the memory needed
	// for the results using malloc(); it's the caller's responsibility to free this when it is no longer needed.
	unsigned int CompressData(const char* data, unsigned int dataSize, bool compressingKey, char*& compressedData);
	// as compressData, but returns the uncompressed size.  Warnings about the output size hold, in spades.
	unsigned int UncompressData(const char* data, unsigned int dataSize, bool compressingKey, char*& uncompressedData);
private:
	unordered_map<string, shared_ptr<Value>>  m_PersistedStoresBackend[numLocks];
	ECompressionTypes                         m_keyCompression, m_valueCompression;
	unsigned int                              m_dataVersion;
	Lock                                      m_partitionLocks[numLocks];
	Lock                                      m_fileLock; // only used if multiple write threads
	bool                                      m_singleWriteThread;
	ofstream                                  m_PersistedFile;
};
