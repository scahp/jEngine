#include "pch.h"
#include "jConsole.h"
#include "jConsoleVariable.h"
#include "External/ImGui/imgui.h"
#include "External/ImGui/imgui_internal.h"
#include <sstream>
#include <algorithm>

jConsole::jConsole()
{
	// Initialize built-in commands first
	RegisterBuiltInCommands();

	// Add initialization message directly without using Mutex
	// (Mutex should be initialized by now, but being safe in constructor)
	LogEntry entry;
	entry.Message = "Console initialized. Press ` to toggle display modes.";
	entry.LogType = LogEntry::Type::Normal;
	LogMessages.push_back(entry);
}

jConsole::~jConsole()
{
}

jConsole& jConsole::Get()
{
	static jConsole instance;
	return instance;
}

void jConsole::Toggle()
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	// Cycle through modes: Hidden -> Small -> Large -> Hidden
	switch (DisplayMode)
	{
	case EConsoleDisplayMode::Hidden:
		DisplayMode = EConsoleDisplayMode::Small;
		bNeedFocusInput = true;
		break;
	case EConsoleDisplayMode::Small:
		DisplayMode = EConsoleDisplayMode::Large;
		bNeedFocusInput = true;
		break;
	case EConsoleDisplayMode::Large:
		DisplayMode = EConsoleDisplayMode::Hidden;
		bNeedFocusInput = false;
		break;
	}
}

void jConsole::SetDisplayMode(EConsoleDisplayMode mode)
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	if (DisplayMode != mode)
	{
		DisplayMode = mode;
		if (mode != EConsoleDisplayMode::Hidden)
			bNeedFocusInput = true;
		else
			bNeedFocusInput = false;
	}
}

void jConsole::RegisterVariable(IConsoleVariable* variable)
{
	if (!variable)
		return;

	std::lock_guard<std::recursive_mutex> lock(Mutex);

	const std::string& name = variable->GetName();

	// Check if variable already exists
	if (Variables.find(name) != Variables.end())
	{
		LogWarning("Console variable '" + name + "' already registered. Overwriting.");
	}

	Variables[name] = variable;
}

IConsoleVariable* jConsole::FindVariable(const std::string& name)
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	auto it = Variables.find(name);
	if (it != Variables.end())
		return it->second;

	return nullptr;
}

void jConsole::RegisterCommand(const std::string& name, CommandFunc func, const std::string& description)
{
	if (!func)
		return;

	std::lock_guard<std::recursive_mutex> lock(Mutex);

	if (Commands.find(name) != Commands.end())
	{
		LogWarning("Console command '" + name + "' already registered. Overwriting.");
	}

	CommandInfo info;
	info.Function = func;
	info.Description = description;
	Commands[name] = info;
}

void jConsole::RegisterBuiltInCommands()
{
	// clear command
	RegisterCommand("clear", [this](const std::vector<std::string>& args) {
		ClearLog();
	}, "Clear console output");

	// list command - list all variables
	RegisterCommand("list", [this](const std::vector<std::string>& args) {
		Log("=== Console Variables ===");
		if (Variables.empty())
		{
			Log("  No variables registered.");
		}
		else
		{
			std::vector<std::string> varNames;
			for (const auto& pair : Variables)
				varNames.push_back(pair.first);
			std::sort(varNames.begin(), varNames.end());

			for (const auto& name : varNames)
			{
				IConsoleVariable* var = Variables[name];
				Log("  " + name + " = " + var->GetAsString());
			}
		}
	}, "List all console variables");

	// find command - search variables by pattern
	RegisterCommand("find", [this](const std::vector<std::string>& args) {
		if (args.size() < 2)
		{
			LogError("Usage: find <pattern>");
			return;
		}

		std::string pattern = args[1];
		std::vector<std::string> matches;

		for (const auto& pair : Variables)
		{
			if (pair.first.find(pattern) != std::string::npos)
				matches.push_back(pair.first);
		}

		if (matches.empty())
		{
			Log("No variables matching '" + pattern + "'");
		}
		else
		{
			Log("Variables matching '" + pattern + "':");
			std::sort(matches.begin(), matches.end());
			for (const auto& name : matches)
			{
				IConsoleVariable* var = Variables[name];
				Log("  " + name + " = " + var->GetAsString());
			}
		}
	}, "Find variables matching a pattern");
}

std::vector<std::string> jConsole::GetAutocompleteSuggestions(const std::string& input)
{
	std::vector<std::string> suggestions;

	if (input.empty())
		return suggestions;

	// Check variables
	for (const auto& pair : Variables)
	{
		if (pair.first.find(input) == 0)  // Starts with input
			suggestions.push_back(pair.first);
	}

	// Check commands
	for (const auto& pair : Commands)
	{
		if (pair.first.find(input) == 0)  // Starts with input
			suggestions.push_back(pair.first);
	}

	std::sort(suggestions.begin(), suggestions.end());
	return suggestions;
}

void jConsole::ApplyAutocomplete(const std::string& suggestion)
{
	if (suggestion.empty())
		return;

	// Copy suggestion to input buffer
	strncpy_s(InputBuffer, sizeof(InputBuffer), suggestion.c_str(), sizeof(InputBuffer) - 1);
	InputBuffer[sizeof(InputBuffer) - 1] = '\0';

	// Set flag to move cursor to end on next frame
	bNeedMoveCursorToEnd = true;
}

void jConsole::UpdateSuggestions(const std::string& input)
{
	// Only reset selection if input actually changed
	bool inputChanged = (input != LastInputForSuggestions);

	if (inputChanged)
	{
		CurrentSuggestions.clear();
		SelectedSuggestionIndex = -1;
		LastInputForSuggestions = input;

		if (input.empty())
			return;

		// Get suggestions that start with input
		CurrentSuggestions = GetAutocompleteSuggestions(input);

		// Limit to 10 suggestions for UI
		if (CurrentSuggestions.size() > 10)
			CurrentSuggestions.resize(10);
	}
}

void jConsole::RenderSuggestions()
{
	if (CurrentSuggestions.empty())
		return;

	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// Calculate popup size based on number of suggestions
	const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
	const float headerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	const float separatorHeight = ImGui::GetStyle().ItemSpacing.y;
	const float popupHeight = headerHeight + separatorHeight + (itemHeight * CurrentSuggestions.size()) + ImGui::GetStyle().WindowPadding.y * 2;
	const float popupWidth = 400.0f;  // Fixed width for suggestions popup

	// Determine popup position based on display mode
	ImVec2 popupPos;
	if (DisplayMode == EConsoleDisplayMode::Small)
	{
		// Small mode: Show popup above the console window (above input field)
		ImVec2 consolePos = ImGui::GetWindowPos();
		float consoleHeight = ImGui::GetWindowHeight();
		float consoleY = consolePos.y;

		// Position popup just above the console window
		popupPos = ImVec2(consolePos.x + 10.0f, consoleY - popupHeight - 5.0f);
	}
	else  // Large mode
	{
		// Large mode: Show popup below the console window (below input field)
		ImVec2 consolePos = ImGui::GetWindowPos();
		float consoleHeight = ImGui::GetWindowHeight();

		// Position popup just below the console window
		popupPos = ImVec2(consolePos.x + 10.0f, consolePos.y + consoleHeight + 5.0f);
	}

	// Ensure popup stays within viewport bounds
	if (popupPos.y < viewport->Pos.y)
		popupPos.y = viewport->Pos.y;
	if (popupPos.y + popupHeight > viewport->Pos.y + viewport->Size.y)
		popupPos.y = viewport->Pos.y + viewport->Size.y - popupHeight;
	if (popupPos.x + popupWidth > viewport->Pos.x + viewport->Size.x)
		popupPos.x = viewport->Pos.x + viewport->Size.x - popupWidth;

	// Render popup window
	ImGui::SetNextWindowPos(popupPos);
	ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight));
	ImGui::SetNextWindowBgAlpha(0.95f);

	ImGuiWindowFlags popupFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav;

	if (ImGui::Begin("##ConsoleSuggestions", nullptr, popupFlags))
	{
		// Keep suggestions popup on top
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		// Header
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Suggestions:");
		ImGui::Separator();

		// Render suggestion items
		for (size_t i = 0; i < CurrentSuggestions.size(); ++i)
		{
			bool isSelected = (i == SelectedSuggestionIndex);

			ImVec4 color = isSelected ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
			ImVec4 bgColor = isSelected ? ImVec4(0.3f, 0.3f, 0.0f, 0.5f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

			// Highlight selected item with background
			if (isSelected)
			{
				ImVec2 itemMin = ImGui::GetCursorScreenPos();
				ImVec2 itemMax = ImVec2(itemMin.x + popupWidth - ImGui::GetStyle().WindowPadding.x * 2,
				                         itemMin.y + itemHeight);
				ImGui::GetWindowDrawList()->AddRectFilled(itemMin, itemMax, ImGui::ColorConvertFloat4ToU32(bgColor));
			}

			if (isSelected)
			{
				ImGui::TextColored(color, "> %s", CurrentSuggestions[i].c_str());
			}
			else
			{
				ImGui::TextColored(color, "  %s", CurrentSuggestions[i].c_str());
			}
		}
	}
	ImGui::End();
}

void jConsole::Render()
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	// Check for ` key to open console even when hidden
	// Use repeat=false to prevent multiple toggles from a single key press
	if (DisplayMode == EConsoleDisplayMode::Hidden)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
		{
			DisplayMode = EConsoleDisplayMode::Small;
			bNeedFocusInput = true;
		}
		return;
	}

	switch (DisplayMode)
	{
	case EConsoleDisplayMode::Small:
		RenderSmall();
		break;
	case EConsoleDisplayMode::Large:
		RenderLarge();
		break;
	default:
		break;
	}
}

void jConsole::RenderLarge()
{
	// Get viewport size
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImVec2 viewportSize = viewport->Size;

	// Top half of screen
	ImVec2 windowSize(viewportSize.x, viewportSize.y * 0.5f);
	ImVec2 windowPos(0, 0);

	ImGui::SetNextWindowPos(windowPos);
	ImGui::SetNextWindowSize(windowSize);
	ImGui::SetNextWindowBgAlpha(0.95f);

	ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin("Console##Large", nullptr, windowFlags))
	{
		// Keep console window on top
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		// Header
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Console (Large Mode)");
		ImGui::Separator();

		RenderLogOutput();
		ImGui::Separator();
		RenderInputField();

		// Render suggestions popup (must be called inside Begin/End)
		RenderSuggestions();
	}
	ImGui::End();
}

void jConsole::RenderSmall()
{
	// Get viewport size
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImVec2 viewportSize = viewport->Size;

	// Bottom small window (about 1/4 of screen height)
	ImVec2 windowSize(viewportSize.x, viewportSize.y * 0.25f);
	ImVec2 windowPos(0, viewportSize.y - windowSize.y);

	ImGui::SetNextWindowPos(windowPos);
	ImGui::SetNextWindowSize(windowSize);
	ImGui::SetNextWindowBgAlpha(0.95f);

	ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin("Console##Small", nullptr, windowFlags))
	{
		// Keep console window on top
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		// Header
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Console (Small Mode)");
		ImGui::Separator();

		RenderLogOutput();
		ImGui::Separator();
		RenderInputField();

		// Render suggestions popup (must be called inside Begin/End)
		RenderSuggestions();
	}
	ImGui::End();
}

void jConsole::RenderLogOutput()
{
	// Log output area with scrolling
	ImGui::BeginChild("LogOutput", ImVec2(0, -30), true, ImGuiWindowFlags_HorizontalScrollbar);

	for (const auto& entry : LogMessages)
	{
		ImVec4 color(1, 1, 1, 1);  // Default white

		switch (entry.LogType)
		{
		case LogEntry::Type::Warning:
			color = ImVec4(1, 1, 0, 1);  // Yellow
			break;
		case LogEntry::Type::Error:
			color = ImVec4(1, 0, 0, 1);  // Red
			break;
		default:
			break;
		}

		ImGui::TextColored(color, "%s", entry.Message.c_str());
	}

	// Auto-scroll to bottom when new messages arrive
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
}

void jConsole::RenderInputField()
{
	// Check for ESC or ` key to close/toggle console
	// Do this before InputText to catch the keys
	// Use repeat=false to only detect initial key press, not repeated presses
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
	{
		SetDisplayMode(EConsoleDisplayMode::Hidden);
		return;
	}

	// Check for ` key (grave accent) to toggle
	// Use repeat=false to prevent multiple toggles from a single key press
	if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
	{
		Toggle();
		return;
	}

	// Input field
	ImGui::Text(">");
	ImGui::SameLine();

	// Always keep focus on input field when console is visible
	// This ensures user can always type without clicking
	if (bNeedFocusInput || !ImGui::IsAnyItemActive())
	{
		ImGui::SetKeyboardFocusHere();
		bNeedFocusInput = false;
	}

	ImGuiInputTextFlags inputFlags =
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_CallbackHistory |
		ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackAlways;

	// Input field callback for history navigation and autocomplete
	auto inputCallback = [](ImGuiInputTextCallbackData* data) -> int
	{
		jConsole* console = (jConsole*)data->UserData;

		switch (data->EventFlag)
		{
		case ImGuiInputTextFlags_CallbackAlways:
		{
			// Move cursor to end if autocomplete was applied
			if (console->bNeedMoveCursorToEnd)
			{
				data->CursorPos = data->BufTextLen;
				data->SelectionStart = data->SelectionEnd = data->CursorPos;
				console->bNeedMoveCursorToEnd = false;
			}

			// Update suggestions as user types
			std::string currentInput(data->Buf, data->BufTextLen);
			console->UpdateSuggestions(currentInput);
			break;
		}
		case ImGuiInputTextFlags_CallbackHistory:
		{
			// If suggestions are active, navigate them
			if (!console->CurrentSuggestions.empty())
			{
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					// Move selection up
					if (console->SelectedSuggestionIndex <= 0)
						console->SelectedSuggestionIndex = (int32)console->CurrentSuggestions.size() - 1;
					else
						console->SelectedSuggestionIndex--;
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					// Move selection down
					if (console->SelectedSuggestionIndex < 0 ||
					    console->SelectedSuggestionIndex >= (int32)console->CurrentSuggestions.size() - 1)
						console->SelectedSuggestionIndex = 0;
					else
						console->SelectedSuggestionIndex++;
				}
			}
			else if (!console->History.empty())
			{
				// Navigate history
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					// Go back in history
					if (console->HistoryIndex == -1)
						console->HistoryIndex = (int32)console->History.size() - 1;
					else if (console->HistoryIndex > 0)
						console->HistoryIndex--;

					if (console->HistoryIndex >= 0 && console->HistoryIndex < (int32)console->History.size())
					{
						data->DeleteChars(0, data->BufTextLen);
						data->InsertChars(0, console->History[console->HistoryIndex].c_str());
					}
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					// Go forward in history
					if (console->HistoryIndex != -1)
					{
						console->HistoryIndex++;
						if (console->HistoryIndex >= (int32)console->History.size())
						{
							console->HistoryIndex = -1;
							data->DeleteChars(0, data->BufTextLen);
						}
						else
						{
							data->DeleteChars(0, data->BufTextLen);
							data->InsertChars(0, console->History[console->HistoryIndex].c_str());
						}
					}
				}
			}
			break;
		}
		case ImGuiInputTextFlags_CallbackCompletion:
		{
			// Tab key pressed - autocomplete
			std::string currentInput(data->Buf, data->BufTextLen);
			auto suggestions = console->GetAutocompleteSuggestions(currentInput);

			if (suggestions.empty())
			{
				// No suggestions
				break;
			}
			else if (suggestions.size() == 1)
			{
				// Single match - autocomplete
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, suggestions[0].c_str());
			}
			else
			{
				// Multiple matches - show them
				console->Log("Autocomplete suggestions:");
				for (const auto& suggestion : suggestions)
				{
					console->Log("  " + suggestion);
				}
			}
			break;
		}
		}
		return 0;
	};

	if (ImGui::InputText("##Input", InputBuffer, sizeof(InputBuffer), inputFlags, inputCallback, this))
	{
		// Check if a suggestion is selected
		if (SelectedSuggestionIndex >= 0 && SelectedSuggestionIndex < (int32)CurrentSuggestions.size())
		{
			// Apply selected suggestion
			std::string selected = CurrentSuggestions[SelectedSuggestionIndex];
			ApplyAutocomplete(selected);

			// Clear suggestions
			CurrentSuggestions.clear();
			SelectedSuggestionIndex = -1;
			LastInputForSuggestions.clear();

			bNeedFocusInput = true;
		}
		else
		{
			// Execute command on Enter
			std::string command(InputBuffer);
			if (!command.empty())
			{
				// Add to history
				History.push_back(command);
				HistoryIndex = -1;

				// Echo command
				Log("> " + command);

				// Execute
				ExecuteCommand(command);

				// Clear input
				InputBuffer[0] = '\0';

				// Clear suggestions
				CurrentSuggestions.clear();
				SelectedSuggestionIndex = -1;
				LastInputForSuggestions.clear();

				bNeedFocusInput = true;
			}
		}
	}
}

void jConsole::Log(const std::string& message)
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	LogEntry entry;
	entry.Message = message;
	entry.LogType = LogEntry::Type::Normal;
	LogMessages.push_back(entry);
}

void jConsole::LogWarning(const std::string& message)
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	LogEntry entry;
	entry.Message = "[WARNING] " + message;
	entry.LogType = LogEntry::Type::Warning;
	LogMessages.push_back(entry);
}

void jConsole::LogError(const std::string& message)
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);

	LogEntry entry;
	entry.Message = "[ERROR] " + message;
	entry.LogType = LogEntry::Type::Error;
	LogMessages.push_back(entry);
}

void jConsole::ClearLog()
{
	std::lock_guard<std::recursive_mutex> lock(Mutex);
	LogMessages.clear();
}

void jConsole::ExecuteCommand(const std::string& commandLine)
{
	// Trim whitespace
	std::string trimmed = commandLine;
	trimmed.erase(0, trimmed.find_first_not_of(" \t"));
	trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

	if (trimmed.empty())
		return;

	// Parse command and arguments
	std::vector<std::string> tokens;
	std::istringstream iss(trimmed);
	std::string token;
	while (iss >> token)
		tokens.push_back(token);

	if (tokens.empty())
		return;

	std::string command = tokens[0];

	// Special help command
	if (command == "help")
	{
		if (tokens.size() > 1)
		{
			// Help for specific variable
			std::string varName = tokens[1];
			IConsoleVariable* var = FindVariable(varName);
			if (var)
			{
				Log("Variable: " + var->GetName());
				Log("Type: " + std::string(
					var->GetType() == EConsoleVariableType::Bool ? "Bool" :
					var->GetType() == EConsoleVariableType::Int ? "Int" :
					var->GetType() == EConsoleVariableType::Float ? "Float" : "String"));
				Log("Description: " + var->GetDescription());
				Log("Current value: " + var->GetAsString());
			}
			else
			{
				LogError("Variable '" + varName + "' not found.");
			}
		}
		else
		{
			// General help
			Log("=== Console Commands ===");
			if (Commands.empty())
			{
				Log("  No commands registered.");
			}
			else
			{
				std::vector<std::string> cmdNames;
				for (const auto& pair : Commands)
					cmdNames.push_back(pair.first);
				std::sort(cmdNames.begin(), cmdNames.end());

				for (const auto& name : cmdNames)
				{
					const auto& info = Commands[name];
					Log("  " + name + " - " + info.Description);
				}
			}
			Log("  help [variable] - Show help for specific variable");
			Log("");
			Log("=== Console Variables ===");

			if (Variables.empty())
			{
				Log("  No variables registered yet.");
			}
			else
			{
				std::vector<std::string> varNames;
				for (const auto& pair : Variables)
					varNames.push_back(pair.first);
				std::sort(varNames.begin(), varNames.end());

				for (const auto& name : varNames)
				{
					IConsoleVariable* var = Variables[name];
					Log("  " + name + " = " + var->GetAsString() + "  (" + var->GetDescription() + ")");
				}
			}

			Log("");
			Log("Usage: <variable> [value]");
			Log("  Example: debug.draw  (query value)");
			Log("  Example: debug.draw true  (set value)");
		}
		return;
	}

	// Check if this is a registered command
	auto cmdIt = Commands.find(command);
	if (cmdIt != Commands.end())
	{
		// Execute registered command
		cmdIt->second.Function(tokens);
		return;
	}

	// Check if this is a console variable
	IConsoleVariable* var = FindVariable(command);
	if (var)
	{
		if (tokens.size() == 1)
		{
			// Query variable value
			Log(command + " = " + var->GetAsString());
		}
		else
		{
			// Set variable value
			std::string value = tokens[1];

			// For string variables, concatenate all remaining tokens
			if (var->GetType() == EConsoleVariableType::String && tokens.size() > 2)
			{
				std::ostringstream oss;
				for (size_t i = 1; i < tokens.size(); ++i)
				{
					if (i > 1)
						oss << " ";
					oss << tokens[i];
				}
				value = oss.str();
			}

			var->SetFromString(value);
			Log(command + " = " + var->GetAsString());
		}
		return;
	}

	// Unknown command
	LogError("Unknown command or variable: " + command);
	Log("Type 'help' for available commands and variables.");
}
