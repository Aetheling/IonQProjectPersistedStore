#pragma once

// use for internal storage.  Cast pointer to whatever you like to char*, and give size.
// Cast back on retrieval.
class Value
{
public:
	Value();
	// this gives ownership of the char array to the Value object
	Value(unsigned int size, char* data);
	// note the reverse order of the arguments.  THIS constructor copies the data, as needed by the template
	Value(char* data, unsigned int size);
	Value(Value& v);
	Value(Value&& v);

	~Value();

	bool operator==(Value& toCompare);
	bool operator!=(Value& toCompare);

	static void Serialize(Value value, char*& serializedValue, unsigned int& serializedSize);
	unsigned int m_size;
	char*        m_data;
};
