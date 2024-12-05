#include "Value.h"
#include <iostream>

int extentValues = 0; // debug emove todo
Value::Value()
{
	m_size = 0;
	m_data = nullptr;
}
Value::Value(unsigned int size, char* data)
{
	// yes; this gives ownership of the char array to the Value object
	m_size = size;
	m_data = data;
	extentValues++; // debug emove todo
}

// note the reverse order of the arguments.  THIS constructor copies the data, as needed by the template
Value::Value(char* data, unsigned int size)
{
	// yes; this gives ownership of the char array to the Value object
	m_size = size;
	m_data = (char*) malloc(sizeof(char)*m_size);
	memcpy_s(m_data, m_size, data, size);
	extentValues++; // debug emove todo
}

Value::Value(Value& v)
{
	m_size = v.m_size;
	m_data = (char*) malloc(sizeof(char)*m_size);
	memcpy_s(m_data, m_size, v.m_data, v.m_size);
	extentValues++; // debug emove todo
}

Value::Value(Value&& v)
{
	m_size = v.m_size;
	m_data = v.m_data;
	// just in case
	v.m_size = 0;
	v.m_data = nullptr;
}

Value::~Value()
{
	if (m_data) free(m_data);
	m_data = nullptr;
	extentValues--; // debug emove todo
}

bool Value::operator==(Value& toCompare)
{
	return((m_size == toCompare.m_size) && (0 == memcmp(m_data, toCompare.m_data, m_size)));
}

bool Value::operator!=(Value& toCompare)
{
	return((m_size != toCompare.m_size) || (0 != memcmp(m_data, toCompare.m_data, m_size)));
}

void Value::Serialize(Value value, char*& serializedValue, unsigned int& serializedSize)
{
	serializedSize = value.m_size;
	serializedValue = (char*) malloc(sizeof(char)*serializedSize);
	memcpy_s(serializedValue, serializedSize, value.m_data, serializedSize);
}