#include "PersistedStore.h"
#include <iostream>
#include <fstream>

typedef unique_lock< Lock >  WriteLock;
typedef shared_lock< Lock >  ReadLock;

template <class DataType, unsigned int numLocks>
PersistedStore<DataType, numLocks>::PersistedStore(ECompressionTypes keyCompression, ECompressionTypes valueCompression, bool singleWriteThread, const char* persistedData)
{
	string fileName;
	ifstream persistedDataFile;
	vector<char> keyBuffer;
	char   buffer[64];

	// initialize basic parameters
	m_keyCompression    = keyCompression;
	m_valueCompression  = valueCompression;
	m_singleWriteThread = singleWriteThread;

	// find the latest persisted version.  Note we want this, even if we are passed a file to read from -- we still
	// need to know what file to write TOO.
	m_dataVersion = 0;
	do
	{
		fileName = PersistedStoreName;
		fileName.append(to_string(++m_dataVersion));
		ifstream f(fileName.c_str(), ios::binary | ios::ate);
		if (f.is_open())
		{
			persistedDataFile.swap(f);
		}
		else
		{
			break;
		}
	} while (true);
	if (persistedData)
	{
		// user-supplied file name trumps using the latest version available.
		ifstream f(persistedData, ios::binary | ios::ate);
		if (f.is_open())
		{
			persistedDataFile.swap(f);
		}
		else
		{
			throw exception("requested persisted file not found");
		}
	}
	// m_dataVersion version (the latest version available) is the version to read FROM -- assuming no file explicitly passed
	// Now, read the values from the appropriate persisted file, if any
	if(persistedDataFile.is_open())
	{
		unsigned int compressedKeySize, compressedValueSize;
		size_t fileSize = persistedDataFile.tellg(); // get the file size
		size_t offset = 0; // to check when might read past the end of the file, indicatng bad data (or just done)
		char sizeBuffer[8];
		vector<char> valBuffer;
		char* compressedKey = nullptr;
		char* compressedValue = nullptr;
		unsigned int* pKeySize = (unsigned int*) sizeBuffer, keySize;
		unsigned int* pValSize = (unsigned int*) (sizeBuffer+sizeof(int)), valSize;
		shared_ptr<Value> val;
		persistedDataFile.seekg(0, ios::beg);
		do
		{
			if(fileSize <= offset + 2*sizeof(unsigned int)) break;
			persistedDataFile.read(sizeBuffer,2*sizeof(unsigned int));
			offset += 2*sizeof(unsigned int);
			keySize = *pKeySize;
			valSize = *pValSize;
			if (fileSize < keySize + valSize + offset)
			{
				printf("Final key/value pair incomplete; ignoring\n");
				break;
			}
			keyBuffer.resize(keySize);
			persistedDataFile.read(keyBuffer.data(), keySize);
			offset += keySize;
			valBuffer.resize(valSize);
			persistedDataFile.read(valBuffer.data(), valSize);
			offset += valSize;
			// compress key and value, as needed
			compressedKeySize = CompressData(keyBuffer.data(), keySize, true, compressedKey);
			compressedValueSize = CompressData(valBuffer.data(), valSize, false, compressedValue);
			// write the key/value pair to the appropriate store, based on the lock it belongs to.
			string key(compressedKey);
			free(compressedKey); // don't need it anymore
			compressedKey = nullptr;
			shared_ptr<Value> val = make_shared<Value>(Value(compressedValueSize, compressedValue)); // val owns compressedValue now; no need to free
			compressedValue = nullptr;
			unsigned int lockID = GetLockID(key);
			auto f = m_PersistedStoresBackend[lockID].insert({ key, val });
			if (!f.second)
			{
				// key already in the store; replace its value with the new
				f.first->second = val;
			}
		} while (true);
	}
	persistedDataFile.close();
	// open the new persisted file, and seed it with the current data (if any)
	// Note each time the process restarts, it starts a new persisted file.
	fileName = PersistedStoreName;
	fileName.append(to_string(m_dataVersion));
	ofstream f(fileName.c_str());
	if (f.is_open())
	{
		m_PersistedFile.swap(f);
		char* uncompressedKey = nullptr;
		char* uncompressedVal = nullptr;
		unsigned int uncompressedKeySize, uncompressedValSize;
		for (auto store : m_PersistedStoresBackend)
		{
			for (auto keyValue : store)
			{
				// use keyBuffer to construct the data to write to the store.  Note that the data in the store is compressed!  Need to uncompress before writing to file
				// uncompress key
				uncompressedKeySize = UncompressData(keyValue.first.data(), keyValue.first.length(), true, uncompressedKey);
				// uncompress value
				uncompressedValSize = UncompressData(keyValue.second->m_data, keyValue.second->m_size, false, uncompressedVal);
				// format: <key size><data size><key value><data value>
				unsigned int writeSize = sizeof(int)*2 + uncompressedKeySize + uncompressedValSize;
				keyBuffer.resize(writeSize);
				((unsigned int*) keyBuffer.data())[0] = uncompressedKeySize;
				((unsigned int*) keyBuffer.data())[1] = uncompressedValSize;
				// add key value to buffer
				size_t offset = 2*sizeof(unsigned int);
				memcpy_s(keyBuffer.data() + offset, writeSize - offset, uncompressedKey, uncompressedKeySize);
				offset += uncompressedKeySize;
				// add data value to the buffer
				memcpy_s(keyBuffer.data() + offset, writeSize - offset, uncompressedVal, uncompressedValSize);
				// persist to file
				m_PersistedFile.write(keyBuffer.data(), writeSize);
				// clean up buffers
				free(uncompressedKey);
				free(uncompressedVal);
				uncompressedKey = nullptr;
				uncompressedVal = nullptr;
			}
		}
	}
	else
	{
		throw exception("couldn't create new persisted file");
	}
}

template <class DataType, unsigned int numLocks>
PersistedStore<DataType, numLocks>::~PersistedStore()
{
	// Note that closing the file automatically flushes all yet-to-write contents
	m_PersistedFile.close();
}

template <class DataType, unsigned int numLocks>
unsigned int PersistedStore<DataType, numLocks>::CompressData(const char* data, unsigned int dataSize, bool compressingKey, char*& compressedData)
{
	unsigned int compressedSize;
	ECompressionTypes eAlgorithm = (compressingKey) ? m_keyCompression : m_valueCompression;
	switch (eAlgorithm)
	{
	    case EReverseString:
			// at least the compressed size is easy -- it's the same
			compressedSize = dataSize;
			compressedData = (char*) malloc(sizeof(char)*(compressedSize+1)); // +1: string -- key -- needs extra space for null terminal
			for(unsigned int i=0; i<compressedSize; i++)
			{
				compressedData[compressedSize-1-i] = data[i];
			}
			compressedData[compressedSize] = '\0'; // needed for key; harmless for data
			break;
		default:
			// no compression: just copy.  Again, final size easy
			compressedSize = dataSize;
			compressedData = (char*) malloc(sizeof(char)*(compressedSize+1)); // +1: string -- key -- needs extra space for null terminal
			memcpy_s(compressedData, compressedSize, data, dataSize);
			compressedData[compressedSize] = '\0'; // needed for key; harmless for data
			break;
	}
	return compressedSize;
}

template <class DataType, unsigned int numLocks>
unsigned int PersistedStore<DataType, numLocks>::UncompressData(const char* data, unsigned int dataSize, bool compressingKey, char*& uncompressedData)
{
	unsigned int uncompressedSize;
	ECompressionTypes eAlgorithm = (compressingKey) ? m_keyCompression : m_valueCompression;
	switch (eAlgorithm)
	{
	    case EReverseString:
			// at least the compressed size is easy -- it's the same
			uncompressedSize = dataSize;
			uncompressedData = (char*) malloc(sizeof(char)*uncompressedSize);
			for(unsigned int i=0; i<uncompressedSize; i++)
			{
				uncompressedData[uncompressedSize-1-i] = data[i];
			}
			break;
		default:
			// no compression: just copy.  Again, final size easy
			uncompressedSize = dataSize;
			uncompressedData = (char*) malloc(sizeof(char)*uncompressedSize);
			memcpy_s(uncompressedData, uncompressedSize, data, dataSize);
			break;
	}
	return uncompressedSize;
}

template <class DataType, unsigned int numLocks>
int PersistedStore<DataType, numLocks>::GetIntegerValueFromString(const char* s)
{
	char c;
	int  val = 0;
    if(nullptr==s || (c = *s++) == '\0') return -1;
	do
	{
		if(c<'0' || '9'<c) return -1;
		val = 10*val + c-'0';
		c = *s++;
	}
	while(c != '\0');
	return val;
}

template <class DataType, unsigned int numLocks>
unsigned int PersistedStore<DataType, numLocks>::GetLockID(string& key)
{
	// The actual hash function should be chosen with the keys in mind, with extra care
	// taken if the data is also partitioned.
	// For now, a simple string hash
	return hash<string>{}(key)%numLocks;
}

template <class DataType, unsigned int numLocks>
shared_ptr<DataType> PersistedStore<DataType, numLocks>::get(string& key)
{
	unsigned int size;
	char* compressedKeyData = nullptr;
	shared_ptr<Value> pData = nullptr;
	shared_ptr<DataType> foundData = nullptr;
	// get the compressed key used by the store
	size = CompressData(key.c_str(), key.length(), true, compressedKeyData);
	string compressedKey(compressedKeyData);
	free(compressedKeyData); // not needed anymore; avoid leaks
	_ASSERT(compressedKey.length() == size);
	unsigned int lockID = GetLockID(compressedKey);
	{
		// scope for lock action
		ReadLock rl(m_partitionLocks[lockID]);
		auto f = m_PersistedStoresBackend[lockID].find(compressedKey);
		if (m_PersistedStoresBackend[lockID].end() != f)
		{
			pData = f->second;
		}
	}
	if (nullptr != pData)
	{
		// found the key in the store.  Decompress, then deserialize
		char* uncompressedData = nullptr;
		size = UncompressData(pData->m_data, pData->m_size, false, uncompressedData);
		foundData = make_shared<DataType>(DataType(uncompressedData, size));
		free(uncompressedData);
	}
	return foundData;
}

// When we put data to the store, it needs to be added both to the store itself and to the persisted file.
// The consistency mandate means the actual write needs to be done under lock: all threads should see the value at the same time
// (Also writing might not be thread safe.)
// Since the data is arbitrary (string?  .jpeg?  spreadsheet?), it is stored in binary in the file.  Format: <key size><data size><key value><data value>
// To minimze writes, we create a temporary buffer with this data, and do a single file write rather than 4 writes (one for each format piece);
// to minimize work under the lock, this is precomputed (as is the compression).  The work is then passed on to a worker thread to do the actual updates.
template <class DataType, unsigned int numLocks>
void PersistedStore<DataType, numLocks>::put(string& key, DataType &v)
{
	// actually, just puts the data into the write queue for the write worker thread to deal with
	unsigned int size, dataSize, bufferSize, lockID;
	char*        rawData;
	char*        buffer;
	// convert from DataType to raw bytes suitable for entry into the store and persisted file
	DataType::Serialize(v, rawData, dataSize);
	// compress key, value for entry into store
	char* rawCompressedKey = nullptr;
	char* compressedData = nullptr;
	// compress the data, if need be
	size = CompressData(rawData, dataSize, false, compressedData);
	shared_ptr<Value> val = make_shared<Value>(Value(size, compressedData)); // val owns compressedData now -- no need to free
	// compress the key, if need be
	size = CompressData(key.c_str(), key.length(), true, rawCompressedKey);
	string compressedKey(rawCompressedKey);
	free(rawCompressedKey); // but this -- THIS we need to free here
	// construct the buffer to be written to the persisted store
	// format: <key size><data size><key value><data value>
	bufferSize = 2*sizeof(unsigned int) + key.length() + dataSize;
	buffer = (char *) malloc(sizeof(char)*bufferSize);
	// add key size to buffer
	((unsigned int *) buffer)[0] = key.length();
	// add data size to buffer
	((unsigned int *) buffer)[1] = dataSize;
	// add key value to buffer
	memcpy_s(buffer + 2*sizeof(unsigned int), bufferSize - (2*sizeof(unsigned int)), key.c_str(), key.length());
	// add data value to the buffer
	memcpy_s(buffer + 2*sizeof(unsigned int) + key.length(), bufferSize - (2*sizeof(unsigned int) + key.length()), rawData, dataSize);
	// find the appropriate lock (and store)
	lockID = GetLockID(compressedKey);
	{
		// scope for lock
		WriteLock wl(m_partitionLocks[lockID]);
		// write to the store
		auto f = m_PersistedStoresBackend[lockID].insert({ compressedKey, val });
		if (!f.second)
		{
			// key already in the store; replace its value with the new
			f.first->second = val;
		}
		// persist to file
		if (m_singleWriteThread)
		{
			m_PersistedFile.write(buffer, bufferSize);
		}
		else
		{
			// need the file lock, too -- only one file but multiuple read locks
			WriteLock(m_fileLock);
			m_PersistedFile.write(buffer, bufferSize);
		}
		m_PersistedFile.flush();
	}
	free(rawData); // don't need it anymore; no need to leak memory
	free(buffer);
}