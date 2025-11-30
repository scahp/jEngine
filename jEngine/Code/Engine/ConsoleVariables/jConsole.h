#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>

// Forward declarations
class IConsoleVariable;

// Console display modes (toggles with Tilt key: `)
enum class EConsoleDisplayMode
{
	Hidden,        // Closed
	Small,         // Small bottom overlay
	Large          // Large top overlay (half screen)
};

class jConsole
{
public:
	static jConsole& Get();

	// UI Control
	void Toggle();  // Called when ` key is pressed
	void SetDisplayMode(EConsoleDisplayMode mode);  // Directly set display mode
	void Render();
	bool IsVisible() const { return DisplayMode != EConsoleDisplayMode::Hidden; }
	EConsoleDisplayMode GetDisplayMode() const { return DisplayMode; }

	// Console Variable Management
	void RegisterVariable(IConsoleVariable* variable);
	IConsoleVariable* FindVariable(const std::string& name);

	// Console Command Management
	using CommandFunc = std::function<void(const std::vector<std::string>&)>;
	void RegisterCommand(const std::string& name, CommandFunc func, const std::string& description = "");

	// Logging
	void Log(const std::string& message);
	void LogWarning(const std::string& message);
	void LogError(const std::string& message);
	void ClearLog();

	// Command execution
	void ExecuteCommand(const std::string& commandLine);

private:
	jConsole();
	~jConsole();

	// Prevent copying
	jConsole(const jConsole&) = delete;
	jConsole& operator=(const jConsole&) = delete;

	// UI Rendering
	void RenderLarge();  // Top half overlay
	void RenderSmall();  // Bottom small overlay
	void RenderInputField();
	void RenderLogOutput();
	void RenderSuggestions();  // Render autocomplete suggestions

	// Autocomplete
	std::vector<std::string> GetAutocompleteSuggestions(const std::string& input);
	void ApplyAutocomplete(const std::string& suggestion);
	void UpdateSuggestions(const std::string& input);

	// Built-in commands
	void RegisterBuiltInCommands();

	// Member variables
	EConsoleDisplayMode DisplayMode = EConsoleDisplayMode::Hidden;

	// Console variables
	std::unordered_map<std::string, IConsoleVariable*> Variables;

	// Console commands
	struct CommandInfo
	{
		CommandFunc Function;
		std::string Description;
	};
	std::unordered_map<std::string, CommandInfo> Commands;

	// Log entries
	struct LogEntry
	{
		std::string Message;
		enum class Type { Normal, Warning, Error } LogType = Type::Normal;
	};
	std::vector<LogEntry> LogMessages;

	// Input
	char InputBuffer[512] = {};
	bool bNeedFocusInput = false;
	bool bNeedMoveCursorToEnd = false;

	// Autocomplete suggestions
	std::vector<std::string> CurrentSuggestions;
	int32 SelectedSuggestionIndex = -1;
	std::string LastInputForSuggestions;  // Track last input to avoid resetting selection

	// History
	std::vector<std::string> History;
	int32 HistoryIndex = -1;

	// Thread safety
	mutable std::recursive_mutex Mutex;
};
