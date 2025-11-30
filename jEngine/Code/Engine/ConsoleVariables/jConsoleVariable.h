#pragma once

#include <string>

// Console variable types
enum class EConsoleVariableType
{
	Bool,
	Int,
	Float,
	String
};

// Base interface for all console variables
class IConsoleVariable
{
public:
	virtual ~IConsoleVariable() = default;

	virtual EConsoleVariableType GetType() const = 0;
	virtual void SetFromString(const std::string& value) = 0;
	virtual std::string GetAsString() const = 0;
	virtual const std::string& GetName() const = 0;
	virtual const std::string& GetDescription() const = 0;
};

// Bool console variable
class jConsoleVariableBool : public IConsoleVariable
{
public:
	// Constructor with external variable pointer
	jConsoleVariableBool(const std::string& name, bool* valuePtr, const std::string& description = "");

	// Constructor with internal variable (default value)
	jConsoleVariableBool(const std::string& name, bool defaultValue, const std::string& description = "");

	~jConsoleVariableBool();

	// Value access
	bool GetValue() const;
	void SetValue(bool value);

	// IConsoleVariable interface
	EConsoleVariableType GetType() const override { return EConsoleVariableType::Bool; }
	void SetFromString(const std::string& value) override;
	std::string GetAsString() const override;
	const std::string& GetName() const override { return Name; }
	const std::string& GetDescription() const override { return Description; }

private:
	std::string Name;
	std::string Description;
	bool* ValuePtr = nullptr;       // External variable pointer (if provided)
	bool InternalValue = false;     // Internal storage (if no external pointer)
	bool bOwnsValue = false;        // True if using internal storage
};

// Int console variable
class jConsoleVariableInt : public IConsoleVariable
{
public:
	jConsoleVariableInt(const std::string& name, int* valuePtr, const std::string& description = "");
	jConsoleVariableInt(const std::string& name, int defaultValue, const std::string& description = "");
	~jConsoleVariableInt();

	int GetValue() const;
	void SetValue(int value);

	EConsoleVariableType GetType() const override { return EConsoleVariableType::Int; }
	void SetFromString(const std::string& value) override;
	std::string GetAsString() const override;
	const std::string& GetName() const override { return Name; }
	const std::string& GetDescription() const override { return Description; }

private:
	std::string Name;
	std::string Description;
	int* ValuePtr = nullptr;
	int InternalValue = 0;
	bool bOwnsValue = false;
};

// Float console variable
class jConsoleVariableFloat : public IConsoleVariable
{
public:
	jConsoleVariableFloat(const std::string& name, float* valuePtr, const std::string& description = "");
	jConsoleVariableFloat(const std::string& name, float defaultValue, const std::string& description = "");
	~jConsoleVariableFloat();

	float GetValue() const;
	void SetValue(float value);

	EConsoleVariableType GetType() const override { return EConsoleVariableType::Float; }
	void SetFromString(const std::string& value) override;
	std::string GetAsString() const override;
	const std::string& GetName() const override { return Name; }
	const std::string& GetDescription() const override { return Description; }

private:
	std::string Name;
	std::string Description;
	float* ValuePtr = nullptr;
	float InternalValue = 0.0f;
	bool bOwnsValue = false;
};

// String console variable
class jConsoleVariableString : public IConsoleVariable
{
public:
	jConsoleVariableString(const std::string& name, std::string* valuePtr, const std::string& description = "");
	jConsoleVariableString(const std::string& name, const std::string& defaultValue, const std::string& description = "");
	~jConsoleVariableString();

	const std::string& GetValue() const;
	void SetValue(const std::string& value);

	EConsoleVariableType GetType() const override { return EConsoleVariableType::String; }
	void SetFromString(const std::string& value) override;
	std::string GetAsString() const override;
	const std::string& GetName() const override { return Name; }
	const std::string& GetDescription() const override { return Description; }

private:
	std::string Name;
	std::string Description;
	std::string* ValuePtr = nullptr;
	std::string InternalValue;
	bool bOwnsValue = false;
};
