#include "pch.h"
#include "jConsoleVariable.h"
#include "jConsole.h"
#include <sstream>

// ============================================================================
// jConsoleVariableBool
// ============================================================================

jConsoleVariableBool::jConsoleVariableBool(const std::string& name, bool* valuePtr, const std::string& description)
	: Name(name)
	, Description(description)
	, ValuePtr(valuePtr)
	, bOwnsValue(false)
{
	// Register with console
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableBool::jConsoleVariableBool(const std::string& name, bool defaultValue, const std::string& description)
	: Name(name)
	, Description(description)
	, InternalValue(defaultValue)
	, ValuePtr(&InternalValue)
	, bOwnsValue(true)
{
	// Register with console
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableBool::~jConsoleVariableBool()
{
	// Note: We don't unregister from console here as it might be destroyed after console
}

bool jConsoleVariableBool::GetValue() const
{
	return ValuePtr ? *ValuePtr : false;
}

void jConsoleVariableBool::SetValue(bool value)
{
	if (ValuePtr)
		*ValuePtr = value;
}

void jConsoleVariableBool::SetFromString(const std::string& value)
{
	// Parse bool from string
	std::string lower = value;
	for (auto& c : lower)
		c = (char)tolower(c);

	if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
		SetValue(true);
	else if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
		SetValue(false);
	else
	{
		// Invalid value
		jConsole::Get().LogError("Invalid bool value: " + value);
	}
}

std::string jConsoleVariableBool::GetAsString() const
{
	return GetValue() ? "true" : "false";
}

// ============================================================================
// jConsoleVariableInt
// ============================================================================

jConsoleVariableInt::jConsoleVariableInt(const std::string& name, int* valuePtr, const std::string& description)
	: Name(name)
	, Description(description)
	, ValuePtr(valuePtr)
	, bOwnsValue(false)
{
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableInt::jConsoleVariableInt(const std::string& name, int defaultValue, const std::string& description)
	: Name(name)
	, Description(description)
	, InternalValue(defaultValue)
	, ValuePtr(&InternalValue)
	, bOwnsValue(true)
{
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableInt::~jConsoleVariableInt()
{
}

int jConsoleVariableInt::GetValue() const
{
	return ValuePtr ? *ValuePtr : 0;
}

void jConsoleVariableInt::SetValue(int value)
{
	if (ValuePtr)
		*ValuePtr = value;
}

void jConsoleVariableInt::SetFromString(const std::string& value)
{
	try
	{
		int intValue = std::stoi(value);
		SetValue(intValue);
	}
	catch (...)
	{
		jConsole::Get().LogError("Invalid int value: " + value);
	}
}

std::string jConsoleVariableInt::GetAsString() const
{
	return std::to_string(GetValue());
}

// ============================================================================
// jConsoleVariableFloat
// ============================================================================

jConsoleVariableFloat::jConsoleVariableFloat(const std::string& name, float* valuePtr, const std::string& description)
	: Name(name)
	, Description(description)
	, ValuePtr(valuePtr)
	, bOwnsValue(false)
{
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableFloat::jConsoleVariableFloat(const std::string& name, float defaultValue, const std::string& description)
	: Name(name)
	, Description(description)
	, InternalValue(defaultValue)
	, ValuePtr(&InternalValue)
	, bOwnsValue(true)
{
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableFloat::~jConsoleVariableFloat()
{
}

float jConsoleVariableFloat::GetValue() const
{
	return ValuePtr ? *ValuePtr : 0.0f;
}

void jConsoleVariableFloat::SetValue(float value)
{
	if (ValuePtr)
		*ValuePtr = value;
}

void jConsoleVariableFloat::SetFromString(const std::string& value)
{
	try
	{
		float floatValue = std::stof(value);
		SetValue(floatValue);
	}
	catch (...)
	{
		jConsole::Get().LogError("Invalid float value: " + value);
	}
}

std::string jConsoleVariableFloat::GetAsString() const
{
	std::ostringstream oss;
	oss << GetValue();
	return oss.str();
}

// ============================================================================
// jConsoleVariableString
// ============================================================================

jConsoleVariableString::jConsoleVariableString(const std::string& name, std::string* valuePtr, const std::string& description)
	: Name(name)
	, Description(description)
	, ValuePtr(valuePtr)
	, bOwnsValue(false)
{
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableString::jConsoleVariableString(const std::string& name, const std::string& defaultValue, const std::string& description)
	: Name(name)
	, Description(description)
	, InternalValue(defaultValue)
	, ValuePtr(&InternalValue)
	, bOwnsValue(true)
{
	jConsole::Get().RegisterVariable(this);
}

jConsoleVariableString::~jConsoleVariableString()
{
}

const std::string& jConsoleVariableString::GetValue() const
{
	static std::string empty;
	return ValuePtr ? *ValuePtr : empty;
}

void jConsoleVariableString::SetValue(const std::string& value)
{
	if (ValuePtr)
		*ValuePtr = value;
}

void jConsoleVariableString::SetFromString(const std::string& value)
{
	SetValue(value);
}

std::string jConsoleVariableString::GetAsString() const
{
	return GetValue();
}
