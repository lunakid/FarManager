﻿/*
console.cpp

Console functions
*/
/*
Copyright © 2010 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Self:
#include "console.hpp"

// Internal:
#include "imports.hpp"
#include "colormix.hpp"
#include "interf.hpp"
#include "setcolor.hpp"
#include "strmix.hpp"
#include "exception.hpp"

// Platform:

// Common:
#include "common.hpp"
#include "common/2d/algorithm.hpp"
#include "common/algorithm.hpp"
#include "common/enum_substrings.hpp"
#include "common/function_traits.hpp"
#include "common/io.hpp"
#include "common/range.hpp"
#include "common/scope_exit.hpp"

// External:
#include "format.hpp"

//----------------------------------------------------------------------------

static bool sWindowMode;
static bool sEnableVirtualTerminal;


static wchar_t ReplaceControlCharacter(wchar_t const Char)
{
	switch (Char)
	{
	case 0x01: return L'\x263a'; // ☺ white smiling face
	case 0x02: return L'\x263b'; // ☻ black smiling face
	case 0x03: return L'\x2665'; // ♥ black heart suit
	case 0x04: return L'\x2666'; // ♦ black diamond suit
	case 0x05: return L'\x2663'; // ♣ black club suit
	case 0x06: return L'\x2660'; // ♠ black spade suit
	case 0x07: return L'\x2022'; // • bullet
	case 0x08: return L'\x25d8'; // ◘ inverse bullet
	case 0x09: return L'\x25cb'; // ○ white circle
	case 0x0a: return L'\x25d9'; // ◙ inverse white circle
	case 0x0b: return L'\x2642'; // ♂ male sign
	case 0x0c: return L'\x2640'; // ♀ female sign
	case 0x0d: return L'\x266a'; // ♪ eighth note
	case 0x0e: return L'\x266b'; // ♫ beamed eighth notes
	case 0x0f: return L'\x263c'; // ☼ white sun with rays
	case 0x10: return L'\x25ba'; // ► black right - pointing pointer
	case 0x11: return L'\x25c4'; // ◄ black left - pointing pointer
	case 0x12: return L'\x2195'; // ↕ up down arrow
	case 0x13: return L'\x203c'; // ‼ double exclamation mark
	case 0x14: return L'\x00b6'; // ¶ pilcrow sign
	case 0x15: return L'\x00a7'; // § section sign
	case 0x16: return L'\x25ac'; // ▬ black rectangle
	case 0x17: return L'\x21a8'; // ↨ up down arrow with base
	case 0x18: return L'\x2191'; // ↑ upwards arrow
	case 0x19: return L'\x2193'; // ↓ downwards arrow
	case 0x1a: return L'\x2192'; // → rightwards arrow
	case 0x1b: return L'\x2190'; // ← leftwards arrow
	case 0x1c: return L'\x221f'; // ∟ right angle
	case 0x1d: return L'\x2194'; // ↔ left right arrow
	case 0x1e: return L'\x25b2'; // ▲ black up - pointing triangle
	case 0x1f: return L'\x25bc'; // ▼ black down - pointing triangle
	case 0x7f: return L'\x2302'; // ⌂ house

	default: return Char;
	}
}

static short GetDelta(CONSOLE_SCREEN_BUFFER_INFO const& csbi)
{
	return csbi.dwSize.Y - (csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
}

namespace console_detail
{
	// пишем/читаем порциями по 32 K, иначе проблемы.
	const unsigned int MAXSIZE = 32768;

	class external_console
	{
	public:
		NONCOPYABLE(external_console);
		external_console() = default;

		struct ModuleImports
		{
		private:
			os::rtdl::module m_Module{ L"extendedconsole.dll"sv };

		public:
#define DECLARE_IMPORT_FUNCTION(name, ...) os::rtdl::function_pointer<__VA_ARGS__> p ## name{ m_Module, #name }

			DECLARE_IMPORT_FUNCTION(ReadOutput,           BOOL(WINAPI*)(FAR_CHAR_INFO* Buffer, COORD BufferSize, COORD BufferCoord, SMALL_RECT* ReadRegion));
			DECLARE_IMPORT_FUNCTION(WriteOutput,          BOOL(WINAPI*)(const FAR_CHAR_INFO* Buffer, COORD BufferSize, COORD BufferCoord, SMALL_RECT* WriteRegion));
			DECLARE_IMPORT_FUNCTION(Commit,               BOOL(WINAPI*)());
			DECLARE_IMPORT_FUNCTION(GetTextAttributes,    BOOL(WINAPI*)(FarColor* Attributes));
			DECLARE_IMPORT_FUNCTION(SetTextAttributes,    BOOL(WINAPI*)(const FarColor* Attributes));
			DECLARE_IMPORT_FUNCTION(ClearExtraRegions,    BOOL(WINAPI*)(const FarColor* Color, int Mode));
			DECLARE_IMPORT_FUNCTION(GetColorDialog,       BOOL(WINAPI*)(FarColor* Color, BOOL Centered, BOOL AddTransparent));

#undef DECLARE_IMPORT_FUNCTION
		}
		Imports;
	};

	static nifty_counter::buffer<external_console> Storage;
	static auto& ExternalConsole = reinterpret_cast<external_console&>(Storage);

	console::console():
		m_OriginalInputHandle(GetStdHandle(STD_INPUT_HANDLE)),
		m_StreamBuffersOverrider(std::make_unique<stream_buffers_overrider>())
	{
		placement::construct(ExternalConsole);
	}

	console::~console()
	{
		if (m_FileHandle != -1)
			_close(m_FileHandle);

		placement::destruct(ExternalConsole);
	}

	bool console::Allocate() const
	{
		return AllocConsole() != FALSE;
	}

	bool console::Free() const
	{
		return FreeConsole() != FALSE;
	}

	HANDLE console::GetInputHandle() const
	{
		return GetStdHandle(STD_INPUT_HANDLE);
	}

	HANDLE console::GetOutputHandle() const
	{
		return GetStdHandle(STD_OUTPUT_HANDLE);
	}

	HANDLE console::GetErrorHandle() const
	{
		return GetStdHandle(STD_ERROR_HANDLE);
	}

	HANDLE console::GetOriginalInputHandle() const
	{
		return m_OriginalInputHandle;
	}

	HWND console::GetWindow() const
	{
		return GetConsoleWindow();
	}

	bool console::GetSize(COORD& Size) const
	{
		bool Result = false;
		CONSOLE_SCREEN_BUFFER_INFO ConsoleScreenBufferInfo;
		if (GetConsoleScreenBufferInfo(GetOutputHandle(), &ConsoleScreenBufferInfo))
		{
			if (sWindowMode)
			{
				Size.X = ConsoleScreenBufferInfo.srWindow.Right - ConsoleScreenBufferInfo.srWindow.Left + 1;
				Size.Y = ConsoleScreenBufferInfo.srWindow.Bottom - ConsoleScreenBufferInfo.srWindow.Top + 1;
			}
			else
			{
				Size = ConsoleScreenBufferInfo.dwSize;
			}
			Result = true;
		}
		return Result;
	}

	bool console::SetSize(COORD Size) const
	{
		if (!sWindowMode)
			return SetConsoleScreenBufferSize(GetOutputHandle(), Size) != FALSE;

		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);
		csbi.srWindow.Left = 0;
		csbi.srWindow.Right = Size.X - 1;
		csbi.srWindow.Bottom = csbi.dwSize.Y - 1;
		csbi.srWindow.Top = csbi.srWindow.Bottom - (Size.Y - 1);
		COORD WindowCoord = { static_cast<SHORT>(csbi.srWindow.Right - csbi.srWindow.Left + 1), static_cast<SHORT>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1) };
		if (WindowCoord.X > csbi.dwSize.X || WindowCoord.Y > csbi.dwSize.Y)
		{
			WindowCoord.X = std::max(WindowCoord.X, csbi.dwSize.X);
			WindowCoord.Y = std::max(WindowCoord.Y, csbi.dwSize.Y);
			SetConsoleScreenBufferSize(GetOutputHandle(), WindowCoord);

			if (WindowCoord.X > csbi.dwSize.X)
			{
				// windows sometimes uses existing colors to init right region of screen buffer
				FarColor Color;
				GetTextAttributes(Color);
				ClearExtraRegions(Color, CR_RIGHT);
			}
		}

		return SetWindowRect(csbi.srWindow);
	}

	bool console::GetWindowRect(SMALL_RECT& ConsoleWindow) const
	{
		CONSOLE_SCREEN_BUFFER_INFO ConsoleScreenBufferInfo;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &ConsoleScreenBufferInfo))
			return false;

		ConsoleWindow = ConsoleScreenBufferInfo.srWindow;
		return true;
	}

	bool console::SetWindowRect(const SMALL_RECT& ConsoleWindow) const
	{
		return SetConsoleWindowInfo(GetOutputHandle(), true, &ConsoleWindow) != FALSE;
	}

	bool console::GetWorkingRect(SMALL_RECT& WorkingRect) const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi))
			return false;

		WorkingRect.Bottom = csbi.dwSize.Y - 1;
		WorkingRect.Left = 0;
		WorkingRect.Right = WorkingRect.Left + ScrX;
		WorkingRect.Top = WorkingRect.Bottom - ScrY;
		return true;
	}

	string console::GetPhysicalTitle() const
	{
		// Don't use GetConsoleTitle here, it's buggy.
		string Title;
		os::GetWindowText(GetWindow(), Title);
		return Title;
	}

	string console::GetTitle() const
	{
		return m_Title;
	}

	bool console::SetTitle(const string& Title) const
	{
		m_Title = Title;
		return SetConsoleTitle(Title.c_str()) != FALSE;
	}

	bool console::GetKeyboardLayoutName(string &strName) const
	{
		wchar_t Buffer[KL_NAMELENGTH];
		if (!imports.GetConsoleKeyboardLayoutNameW(Buffer))
			return false;

		strName = Buffer;
		return true;
	}

	uintptr_t console::GetInputCodepage() const
	{
		return GetConsoleCP();
	}

	bool console::SetInputCodepage(uintptr_t Codepage) const
	{
		return SetConsoleCP(Codepage) != FALSE;
	}

	uintptr_t console::GetOutputCodepage() const
	{
		return GetConsoleOutputCP();
	}

	bool console::SetOutputCodepage(uintptr_t Codepage) const
	{
		return SetConsoleOutputCP(Codepage) != FALSE;
	}

	bool console::SetControlHandler(PHANDLER_ROUTINE HandlerRoutine, bool Add) const
	{
		return SetConsoleCtrlHandler(HandlerRoutine, Add) != FALSE;
	}

	bool console::GetMode(HANDLE ConsoleHandle, DWORD& Mode) const
	{
		return GetConsoleMode(ConsoleHandle, &Mode) != FALSE;
	}

	bool console::SetMode(HANDLE ConsoleHandle, DWORD Mode) const
	{
		return SetConsoleMode(ConsoleHandle, Mode) != FALSE;
	}

	static void AdjustMouseEvents(INPUT_RECORD* Buffer, size_t Length, short Delta, short MaxX)
	{
		for (auto& i: span(Buffer, Length))
		{
			if (i.EventType == MOUSE_EVENT)
			{
				i.Event.MouseEvent.dwMousePosition.Y = std::max(0, i.Event.MouseEvent.dwMousePosition.Y - Delta);
				i.Event.MouseEvent.dwMousePosition.X = std::min(i.Event.MouseEvent.dwMousePosition.X, MaxX);
			}
		}
	}

	bool console::PeekInput(INPUT_RECORD* Buffer, size_t Length, size_t& NumberOfEventsRead) const
	{
		DWORD dwNumberOfEventsRead = 0;
		bool Result = PeekConsoleInput(GetInputHandle(), Buffer, static_cast<DWORD>(Length), &dwNumberOfEventsRead) != FALSE;
		NumberOfEventsRead = dwNumberOfEventsRead;
		if (sWindowMode)
		{
			COORD Size = {};
			GetSize(Size);
			AdjustMouseEvents(Buffer, NumberOfEventsRead, GetDelta(), Size.X - 1);
		}
		return Result;
	}

	bool console::ReadInput(INPUT_RECORD* Buffer, size_t Length, size_t& NumberOfEventsRead) const
	{
		DWORD dwNumberOfEventsRead = 0;
		if (!ReadConsoleInput(GetInputHandle(), Buffer, static_cast<DWORD>(Length), &dwNumberOfEventsRead))
			return false;

		NumberOfEventsRead = dwNumberOfEventsRead;

		if (sWindowMode)
		{
			COORD Size = {};
			GetSize(Size);
			AdjustMouseEvents(Buffer, NumberOfEventsRead, GetDelta(), Size.X - 1);
		}

		return true;
	}

	bool console::WriteInput(INPUT_RECORD* Buffer, size_t Length, size_t& NumberOfEventsWritten) const
	{
		if (sWindowMode)
		{
			const auto Delta = GetDelta();

			for (auto& i: span(Buffer, Length))
			{
				if (i.EventType == MOUSE_EVENT)
				{
					i.Event.MouseEvent.dwMousePosition.Y += Delta;
				}
			}
		}
		DWORD dwNumberOfEventsWritten = 0;
		bool Result = WriteConsoleInput(GetInputHandle(), Buffer, static_cast<DWORD>(Length), &dwNumberOfEventsWritten) != FALSE;
		NumberOfEventsWritten = dwNumberOfEventsWritten;
		return Result;
	}

	static bool ReadOutputImpl(CHAR_INFO* const Buffer, COORD const BufferSize, SMALL_RECT& ReadRegion)
	{
		return ReadConsoleOutput(::console.GetOutputHandle(), Buffer, BufferSize, {}, &ReadRegion) != FALSE;
	}

	bool console::ReadOutput(matrix<FAR_CHAR_INFO>& Buffer, COORD BufferCoord, const SMALL_RECT& ReadRegionRelative) const
	{
		if (ExternalConsole.Imports.pReadOutput)
		{
			const COORD SizeCoord = { static_cast<SHORT>(Buffer.width()), static_cast<SHORT>(Buffer.height()) };
			auto ReadRegion = ReadRegionRelative;
			return ExternalConsole.Imports.pReadOutput(Buffer.data(), SizeCoord, BufferCoord, &ReadRegion) != FALSE;
		}

		const int Delta = sWindowMode? GetDelta() : 0;
		auto ReadRegion = ReadRegionRelative;
		ReadRegion.Top += Delta;
		ReadRegion.Bottom += Delta;

		const rectangle SubRect{ BufferCoord.X, BufferCoord.Y, BufferCoord.X + (ReadRegion.Right - ReadRegion.Left + 1) - 1, BufferCoord.Y + (ReadRegion.Bottom - ReadRegion.Top + 1) - 1 };

		std::vector<CHAR_INFO> ConsoleBuffer(SubRect.width() * SubRect.height());

		const COORD BufferSize{ static_cast<SHORT>(SubRect.width()), static_cast<SHORT>(SubRect.height()) };

		if (BufferSize.X * BufferSize.Y * sizeof(CHAR_INFO) > MAXSIZE)
		{
			const auto HeightStep = std::max(MAXSIZE / (BufferSize.X * sizeof(CHAR_INFO)), size_t(1));

			const size_t Height = ReadRegion.Bottom - ReadRegion.Top + 1;

			for (size_t i = 0; i < Height; i += HeightStep)
			{
				auto PartialReadRegion = ReadRegion;
				PartialReadRegion.Top += static_cast<SHORT>(i);
				PartialReadRegion.Bottom = std::min(ReadRegion.Bottom, static_cast<SHORT>(PartialReadRegion.Top + HeightStep - 1));
				const COORD PartialBufferSize{ BufferSize.X, static_cast<SHORT>(PartialReadRegion.Bottom - PartialReadRegion.Top + 1) };
				if (!ReadOutputImpl(ConsoleBuffer.data() + i * PartialBufferSize.X, PartialBufferSize, PartialReadRegion))
					return false;
			}
		}
		else
		{
			auto ReadRegionCopy = ReadRegion;
			if (!ReadOutputImpl(ConsoleBuffer.data(), BufferSize, ReadRegionCopy))
				return false;
		}

		auto ConsoleBufferIterator = ConsoleBuffer.cbegin();
		for_submatrix(Buffer, SubRect, [&](FAR_CHAR_INFO& i)
		{
			const auto& Cell = *ConsoleBufferIterator++;
			i = { Cell.Char.UnicodeChar, colors::ConsoleColorToFarColor(Cell.Attributes) };
		});

		return true;
	}

	// NT is RGB, VT is BGR
	static int rgb_to_bgr(int const RGB)
	{
		return (RGB & 0b100) >> 2 | (RGB & 0b010) | (RGB & 0b001) << 2;
	}

	static wchar_t vt_color_index(COLORREF Color)
	{
		return L'0' + rgb_to_bgr(Color);
	}

	static const struct
	{
		string_view Normal, Intense, TrueColour;
		COLORREF FarColor::* Color;
		FARCOLORFLAGS Flags;
	}
	ColorsMapping[]
	{
		{ L"3"sv,  L"9"sv, L"38"sv, &FarColor::ForegroundColor, FCF_FG_4BIT },
		{ L"4"sv, L"10"sv, L"48"sv, &FarColor::BackgroundColor, FCF_BG_4BIT },
	};

	static void make_vt_attributes(const FarColor& Attributes, string& Str, std::optional<FarColor> const& LastColor)
	{
		append(Str, L"\033["sv);

		for (const auto& i: ColorsMapping)
		{
			const auto ColorPart = std::invoke(i.Color, Attributes);

			if (Attributes.Flags & i.Flags)
			{
				append(Str, ColorPart & FOREGROUND_INTENSITY? i.Intense : i.Normal, vt_color_index(ColorPart));
			}
			else
			{
				const union { COLORREF Color; rgba RGBA; } Value { ColorPart };
				Str += format(FSTR(L"{0};2;{1};{2};{3}"), i.TrueColour, Value.RGBA.r, Value.RGBA.g, Value.RGBA.b);
			}

			Str += L';';
		}

		Str.pop_back();

		if (Attributes.Flags & FCF_FG_UNDERLINE)
		{
			if (!LastColor.has_value() || !(LastColor->Flags & FCF_FG_UNDERLINE))
				Str += L";4"sv;
		}
		else
		{
			if (LastColor.has_value() && LastColor->Flags & FCF_FG_UNDERLINE)
				Str += L";24"sv;
		}

		Str += L'm';
	}

	static void make_vt_sequence(span<const FAR_CHAR_INFO> Input, string& Str, std::optional<FarColor>& LastColor)
	{
		for (const auto& i: Input)
		{
			if (!LastColor.has_value() || i.Attributes != *LastColor)
			{
				make_vt_attributes(i.Attributes, Str, LastColor);
				LastColor = i.Attributes;
			}

			Str += ReplaceControlCharacter(i.Char);
		}
	}

	class console::implementation
	{
	public:
		static bool WriteOutputVT(const matrix<FAR_CHAR_INFO>& Buffer, rectangle const SubRect, const SMALL_RECT& WriteRegion)
		{
			const auto Out = ::console.GetOutputHandle();

			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (!GetConsoleScreenBufferInfo(Out, &csbi))
				return false;

			COORD SavedCursorPosition;
			if (!::console.GetCursorRealPosition(SavedCursorPosition))
				return false;

			CONSOLE_CURSOR_INFO SavedCursorInfo;
			if (!::console.GetCursorInfo(SavedCursorInfo))
				return false;

			if (
				// Hide cursor
				!::console.SetCursorInfo({1}) ||
				// Move the viewport down
				!::console.SetCursorRealPosition({ 0, static_cast<SHORT>(csbi.dwSize.Y - 1) }) ||
				// Set cursor position within the viewport
				!::console.SetCursorRealPosition({ WriteRegion.Left, WriteRegion.Top }))
				return false;

			SCOPE_EXIT
			{
				// Move the viewport down
				::console.SetCursorRealPosition({ 0, static_cast<SHORT>(csbi.dwSize.Y - 1) });
				// Restore cursor position within the viewport
				::console.SetCursorRealPosition(SavedCursorPosition);
				// Restore cursor
				::console.SetCursorInfo(SavedCursorInfo);
				// Restore buffer relative position
				if (csbi.srWindow.Left || csbi.srWindow.Bottom != csbi.dwSize.Y - 1)
					::console.SetWindowRect(csbi.srWindow);
			};

			COORD CursorPosition{ WriteRegion.Left, WriteRegion.Top };

			if (sWindowMode)
			{
				CursorPosition.Y -= ::GetDelta(csbi);

				if (CursorPosition.Y < 0)
				{
					// Drawing above the viewport
					CursorPosition.Y = 0;
				}
			}

			string Str;

			std::optional<FarColor> LastColor;

			for (short i = SubRect.top; i <= SubRect.bottom; ++i)
			{
				if (i != SubRect.top)
					Str += format(FSTR(L"\033[{0};{1}H"), CursorPosition.Y + 1 + (i - SubRect.top), CursorPosition.X + 1);

				make_vt_sequence(span(Buffer[i].data() + SubRect.left, SubRect.width()), Str, LastColor);
			}

			append(Str, L"\033[0m"sv);

			return ::console.Write(Str);
		}

		static bool WriteOutputNTImpl(CHAR_INFO const* const Buffer, COORD const BufferSize, SMALL_RECT& WriteRegion)
		{
			return WriteConsoleOutput(::console.GetOutputHandle(), Buffer, BufferSize, {}, &WriteRegion) != FALSE;
		}

		static bool WriteOutputNTImplDebug(CHAR_INFO* const Buffer, COORD const BufferSize, SMALL_RECT& WriteRegion)
		{
#if 0
			assert(BufferSize.X == WriteRegion.Right - WriteRegion.Left + 1);
			assert(BufferSize.Y == WriteRegion.Bottom - WriteRegion.Top + 1);


			for (auto&i: span(Buffer, BufferSize.X * BufferSize.Y))
			{
				i.Attributes = (i.Attributes & FCF_RAWATTR_MASK) | LOBYTE(~i.Attributes);
			}

			auto WriteRegionCopy = WriteRegion;
			WriteOutputNTImpl(Buffer, BufferSize, WriteRegionCopy);
			Sleep(50);

			for (auto&i: span(Buffer, BufferSize.X * BufferSize.Y))
				i.Attributes = (i.Attributes & FCF_RAWATTR_MASK) | LOBYTE(~i.Attributes);
#endif
			return WriteOutputNTImpl(Buffer, BufferSize, WriteRegion) != FALSE;
		}

		static bool WriteOutputNT(const matrix<FAR_CHAR_INFO>& Buffer, rectangle const SubRect, const SMALL_RECT& WriteRegion)
		{
			std::vector<CHAR_INFO> ConsoleBuffer;
			ConsoleBuffer.reserve(SubRect.width() * SubRect.height());

			for_submatrix(Buffer, SubRect, [&](const FAR_CHAR_INFO& i)
			{
				ConsoleBuffer.emplace_back(CHAR_INFO{ { ReplaceControlCharacter(i.Char) }, colors::FarColorToConsoleColor(i.Attributes) });
			});

			const COORD BufferSize{ static_cast<SHORT>(SubRect.width()), static_cast<SHORT>(SubRect.height()) };

			if (BufferSize.X * BufferSize.Y * sizeof(CHAR_INFO) > MAXSIZE)
			{
				const auto HeightStep = std::max(MAXSIZE / (BufferSize.X * sizeof(CHAR_INFO)), size_t(1));

				const size_t Height = WriteRegion.Bottom - WriteRegion.Top + 1;

				for (size_t i = 0; i < Height; i += HeightStep)
				{
					auto PartialWriteRegion = WriteRegion;
					PartialWriteRegion.Top += static_cast<SHORT>(i);
					PartialWriteRegion.Bottom = std::min(WriteRegion.Bottom, static_cast<SHORT>(PartialWriteRegion.Top + HeightStep - 1));
					const COORD PartialBufferSize{ BufferSize.X, static_cast<SHORT>(PartialWriteRegion.Bottom - PartialWriteRegion.Top + 1) };
					if (!WriteOutputNTImplDebug(ConsoleBuffer.data() + i * PartialBufferSize.X, PartialBufferSize, PartialWriteRegion))
						return false;
				}
			}
			else
			{
				auto WriteRegionCopy = WriteRegion;
				if (!WriteOutputNTImplDebug(ConsoleBuffer.data(), BufferSize, WriteRegionCopy))
					return false;
			}

			return true;
		}

		static bool SetTextAttributesVT(const FarColor& Attributes)
		{
			// For fallback
			SetTextAttributesNT(Attributes);

			string Str;
			make_vt_attributes(Attributes, Str, {});
			return ::console.Write(Str);
		}

		static bool SetTextAttributesNT(const FarColor& Attributes)
		{
			return SetConsoleTextAttribute(::console.GetOutputHandle(), colors::FarColorToConsoleColor(Attributes)) != FALSE;
		}
	};

	bool console::WriteOutput(const matrix<FAR_CHAR_INFO>& Buffer, COORD BufferCoord, const SMALL_RECT& WriteRegionRelative) const
	{
		if (ExternalConsole.Imports.pWriteOutput)
		{
			const COORD BufferSize = { static_cast<SHORT>(Buffer.width()), static_cast<SHORT>(Buffer.height()) };
			auto WriteRegion = WriteRegionRelative;
			return ExternalConsole.Imports.pWriteOutput(Buffer.data(), BufferSize, BufferCoord, &WriteRegion) != FALSE;
		}

		const int Delta = sWindowMode? GetDelta() : 0;
		auto WriteRegion = WriteRegionRelative;
		WriteRegion.Top += Delta;
		WriteRegion.Bottom += Delta;

		const rectangle SubRect{ BufferCoord.X, BufferCoord.Y, BufferCoord.X + (WriteRegion.Right - WriteRegion.Left + 1) - 1, BufferCoord.Y + (WriteRegion.Bottom - WriteRegion.Top + 1) - 1 };

		DWORD Mode = 0;
		const auto IsVT = sEnableVirtualTerminal && GetMode(GetOutputHandle(), Mode) && Mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;

		return (IsVT? implementation::WriteOutputVT : implementation::WriteOutputNT)(Buffer, SubRect, WriteRegion);
	}

	bool console::Read(std::vector<wchar_t>& Buffer, size_t& Size) const
	{
		const auto InputHandle = GetInputHandle();

		DWORD NumberOfCharsRead;

		DWORD Mode;
		if (GetMode(InputHandle, Mode))
		{
			if (!ReadConsole(InputHandle, Buffer.data(), static_cast<DWORD>(Buffer.size()), &NumberOfCharsRead, nullptr))
				return false;
		}
		else
		{
			if (!ReadFile(InputHandle, Buffer.data(), static_cast<DWORD>(Buffer.size() * sizeof(wchar_t)), &NumberOfCharsRead, nullptr))
				return false;

			NumberOfCharsRead /= sizeof(wchar_t);
		}

		Size = NumberOfCharsRead;
		return true;
	}

	bool console::Write(const string_view Str) const
	{
		DWORD NumberOfCharsWritten;
		const auto OutputHandle = GetOutputHandle();

		DWORD Mode;
		if (GetMode(OutputHandle, Mode))
			return WriteConsole(OutputHandle, Str.data(), static_cast<DWORD>(Str.size()), &NumberOfCharsWritten, nullptr) != FALSE;

		// Redirected output

		if (m_FileHandle == -1)
		{
			HANDLE OsHandle;
			if (!DuplicateHandle(GetCurrentProcess(), OutputHandle, GetCurrentProcess(), &OsHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
				return false;

			m_FileHandle = _open_osfhandle(reinterpret_cast<intptr_t>(OsHandle), _O_U8TEXT);
			if (m_FileHandle == -1)
				return false;

			_setmode(m_FileHandle, _O_U8TEXT);
		}

		return _write(m_FileHandle, Str.data(), static_cast<unsigned int>(Str.size() * sizeof(wchar_t))) != -1;
	}

	bool console::Commit() const
	{
		if (ExternalConsole.Imports.pCommit)
			return ExternalConsole.Imports.pCommit() != FALSE;

		// reserved
		return true;
	}

	bool console::GetTextAttributes(FarColor& Attributes) const
	{
		if (ExternalConsole.Imports.pGetTextAttributes)
			return ExternalConsole.Imports.pGetTextAttributes(&Attributes) != FALSE;

		CONSOLE_SCREEN_BUFFER_INFO ConsoleScreenBufferInfo;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &ConsoleScreenBufferInfo))
			return false;

		Attributes = colors::ConsoleColorToFarColor(ConsoleScreenBufferInfo.wAttributes);
		return true;
	}

	bool console::SetTextAttributes(const FarColor& Attributes) const
	{
		if (ExternalConsole.Imports.pSetTextAttributes)
			return ExternalConsole.Imports.pSetTextAttributes(&Attributes) != FALSE;

		DWORD Mode;
		const auto IsVT = sEnableVirtualTerminal && GetMode(GetOutputHandle(), Mode) && Mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;

		return (IsVT? implementation::SetTextAttributesVT : implementation::SetTextAttributesNT)(Attributes);
	}

	bool console::GetCursorInfo(CONSOLE_CURSOR_INFO& ConsoleCursorInfo) const
	{
		return GetConsoleCursorInfo(GetOutputHandle(), &ConsoleCursorInfo) != FALSE;
	}

	bool console::SetCursorInfo(const CONSOLE_CURSOR_INFO& ConsoleCursorInfo) const
	{
		return SetConsoleCursorInfo(GetOutputHandle(), &ConsoleCursorInfo) != FALSE;
	}

	bool console::GetCursorPosition(COORD& Position) const
	{
		if (!GetCursorRealPosition(Position))
			return false;

		if (sWindowMode)
			Position.Y -= GetDelta();

		return true;
	}

	bool console::SetCursorPosition(COORD Position) const
	{
		if (sWindowMode)
		{
			COORD Size = {};
			GetSize(Size);
			Position.X = std::min(Position.X, static_cast<SHORT>(Size.X - 1));
			Position.Y = std::max(static_cast<SHORT>(0), Position.Y);
			Position.Y += GetDelta();
		}
		return SetCursorRealPosition(Position);
	}

	bool console::FlushInputBuffer() const
	{
		return FlushConsoleInputBuffer(GetInputHandle()) != FALSE;
	}

	bool console::GetNumberOfInputEvents(size_t& NumberOfEvents) const
	{
		DWORD dwNumberOfEvents = 0;
		const auto Result = GetNumberOfConsoleInputEvents(GetInputHandle(), &dwNumberOfEvents) != FALSE;
		NumberOfEvents = dwNumberOfEvents;
		return Result;
	}

	bool console::GetAlias(string_view const Source, wchar_t* TargetBuffer, size_t TargetBufferLength, string_view const ExeName) const
	{
		return GetConsoleAlias(const_cast<LPWSTR>(null_terminated(Source).c_str()), TargetBuffer, static_cast<DWORD>(TargetBufferLength), const_cast<LPWSTR>(null_terminated(ExeName).c_str())) != 0;
	}

	std::unordered_map<string, std::unordered_map<string, string>> console::GetAllAliases() const
	{
		FN_RETURN_TYPE(console::GetAllAliases) Result;

		const auto ExeLength = GetConsoleAliasExesLength();
		if (!ExeLength)
			return Result;

		std::vector<wchar_t> ExeBuffer(ExeLength / sizeof(wchar_t) + 1); // +1 for double \0
		if (!GetConsoleAliasExes(ExeBuffer.data(), ExeLength))
			return Result;

		std::vector<wchar_t> AliasesBuffer;
		for (const auto& ExeToken : enum_substrings(ExeBuffer.data()))
		{
			// It's ok, ExeToken is guaranteed to be null-terminated
			const auto ExeNamePtr = const_cast<wchar_t*>(ExeToken.data());
			const auto AliasesLength = GetConsoleAliasesLength(ExeNamePtr);
			AliasesBuffer.resize(AliasesLength / sizeof(wchar_t) + 1); // +1 for double \0
			if (!GetConsoleAliases(AliasesBuffer.data(), AliasesLength, ExeNamePtr))
				continue;

			auto& ExeMap = Result[ExeNamePtr];
			for (const auto& AliasToken : enum_substrings(AliasesBuffer.data()))
			{
				ExeMap.emplace(split_name_value(AliasToken));
			}
		}

		return Result;
	}

	void console::SetAllAliases(const std::unordered_map<string, std::unordered_map<string, string>>& Aliases) const
	{
		for (const auto& [ExeName, ExeAliases]: Aliases)
		{
			for (const auto& [Alias, Value]: ExeAliases)
			{
				AddConsoleAlias(
					const_cast<wchar_t*>(Alias.c_str()),
					const_cast<wchar_t*>(Value.c_str()),
					const_cast<wchar_t*>(ExeName.c_str())
				);
			}
		}
	}

	bool console::GetDisplayMode(DWORD& Mode) const
	{
		return GetConsoleDisplayMode(&Mode) != FALSE;
	}

	COORD console::GetLargestWindowSize() const
	{
		COORD Result = GetLargestConsoleWindowSize(GetOutputHandle());
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);
		if (csbi.dwSize.Y > Result.Y)
		{
			CONSOLE_FONT_INFO FontInfo;
			if (GetCurrentConsoleFont(GetOutputHandle(), FALSE, &FontInfo))
			{
				// in XP FontInfo.dwFontSize contains something else than size in pixels.
				FontInfo.dwFontSize = GetConsoleFontSize(GetOutputHandle(), FontInfo.nFont);
				Result.X -= Round(static_cast<SHORT>(GetSystemMetrics(SM_CXVSCROLL)), FontInfo.dwFontSize.X);
			}
		}
		return Result;
	}

	bool console::SetActiveScreenBuffer(HANDLE ConsoleOutput) const
	{
		return SetConsoleActiveScreenBuffer(ConsoleOutput) != FALSE;
	}

	bool console::ClearExtraRegions(const FarColor& Color, int Mode) const
	{
		if (ExternalConsole.Imports.pClearExtraRegions)
			return ExternalConsole.Imports.pClearExtraRegions(&Color, Mode) != FALSE;

		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);
		DWORD CharsWritten;
		const auto ConColor = colors::FarColorToConsoleColor(Color);

		if (Mode&CR_TOP)
		{
			DWORD TopSize = csbi.dwSize.X*csbi.srWindow.Top;
			COORD TopCoord = {};
			FillConsoleOutputCharacter(GetOutputHandle(), L' ', TopSize, TopCoord, &CharsWritten);
			FillConsoleOutputAttribute(GetOutputHandle(), ConColor, TopSize, TopCoord, &CharsWritten);
		}

		if (Mode&CR_RIGHT)
		{
			DWORD RightSize = csbi.dwSize.X - csbi.srWindow.Right;
			COORD RightCoord = { csbi.srWindow.Right, ::GetDelta(csbi) };
			for (; RightCoord.Y < csbi.dwSize.Y; RightCoord.Y++)
			{
				FillConsoleOutputCharacter(GetOutputHandle(), L' ', RightSize, RightCoord, &CharsWritten);
				FillConsoleOutputAttribute(GetOutputHandle(), ConColor, RightSize, RightCoord, &CharsWritten);
			}
		}
		return true;
	}

	bool console::ScrollWindow(int Lines, int Columns) const
	{
		bool process = false;
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);

		if ((Lines < 0 && csbi.srWindow.Top) || (Lines > 0 && csbi.srWindow.Bottom != csbi.dwSize.Y - 1))
		{
			csbi.srWindow.Top += Lines;
			csbi.srWindow.Bottom += Lines;

			if (csbi.srWindow.Top < 0)
			{
				csbi.srWindow.Bottom -= csbi.srWindow.Top;
				csbi.srWindow.Top = 0;
			}

			if (csbi.srWindow.Bottom >= csbi.dwSize.Y)
			{
				csbi.srWindow.Top -= (csbi.srWindow.Bottom - (csbi.dwSize.Y - 1));
				csbi.srWindow.Bottom = csbi.dwSize.Y - 1;
			}
			process = true;
		}

		if ((Columns < 0 && csbi.srWindow.Left) || (Columns > 0 && csbi.srWindow.Right != csbi.dwSize.X - 1))
		{
			csbi.srWindow.Left += Columns;
			csbi.srWindow.Right += Columns;

			if (csbi.srWindow.Left < 0)
			{
				csbi.srWindow.Right -= csbi.srWindow.Left;
				csbi.srWindow.Left = 0;
			}

			if (csbi.srWindow.Right >= csbi.dwSize.X)
			{
				csbi.srWindow.Left -= (csbi.srWindow.Right - (csbi.dwSize.X - 1));
				csbi.srWindow.Right = csbi.dwSize.X - 1;
			}
			process = true;
		}

		return process && SetWindowRect(csbi.srWindow);
	}

	bool console::ScrollWindowToBegin() const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);

		if (csbi.srWindow.Top > 0)
		{
			csbi.srWindow.Bottom -= csbi.srWindow.Top;
			csbi.srWindow.Top = 0;
			return SetWindowRect(csbi.srWindow);
		}

		return false;
	}

	bool console::ScrollWindowToEnd() const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);

		if (csbi.srWindow.Bottom < csbi.dwSize.Y - 1)
		{
			csbi.srWindow.Top += csbi.dwSize.Y - 1 - csbi.srWindow.Bottom;
			csbi.srWindow.Bottom = csbi.dwSize.Y - 1;
			return SetWindowRect(csbi.srWindow);
		}

		return false;
	}

	bool console::IsFullscreenSupported() const
	{
#ifdef _WIN64
		return false;
#else
		CONSOLE_SCREEN_BUFFER_INFOEX csbiex{ sizeof(csbiex) };
		if (imports.GetConsoleScreenBufferInfoEx(GetOutputHandle(), &csbiex))
			return csbiex.bFullscreenSupported != FALSE;

		return true;
#endif
	}

	bool console::ResetPosition() const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);
		if (csbi.srWindow.Left || csbi.srWindow.Bottom != csbi.dwSize.Y - 1)
		{
			csbi.srWindow.Right -= csbi.srWindow.Left;
			csbi.srWindow.Left = 0;
			csbi.srWindow.Top += csbi.dwSize.Y - 1 - csbi.srWindow.Bottom;
			csbi.srWindow.Bottom = csbi.dwSize.Y - 1;
			SetWindowRect(csbi.srWindow);
		}
		return true;
	}

	bool console::ResetViewportPosition() const
	{
		COORD Size;
		return GetSize(Size) && SetCursorPosition({0, static_cast<SHORT>(Size.Y - 1) });
	}

	bool console::GetColorDialog(FarColor& Color, bool const Centered, const FarColor* const BaseColor) const
	{
		if (ExternalConsole.Imports.pGetColorDialog)
			return ExternalConsole.Imports.pGetColorDialog(&Color, Centered, BaseColor != nullptr) != FALSE;

		return GetColorDialogInternal(Color, Centered, BaseColor);
	}

	short console::GetDelta() const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi);
		return ::GetDelta(csbi);
	}

	bool console::ScrollScreenBuffer(const SMALL_RECT& ScrollRectangle, const SMALL_RECT* ClipRectangle, COORD DestinationOrigin, const FAR_CHAR_INFO& Fill) const
	{
		const CHAR_INFO SysFill{ { Fill.Char }, colors::FarColorToConsoleColor(Fill.Attributes) };
		return ScrollConsoleScreenBuffer(GetOutputHandle(), &ScrollRectangle, ClipRectangle, DestinationOrigin, &SysFill) != FALSE;
	}

	bool console::ScrollNonClientArea(size_t NumLines, const FAR_CHAR_INFO& Fill) const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi))
			return false;

		const SMALL_RECT ScrollRectangle{ 0, 0, static_cast<SHORT>(csbi.dwSize.X - 1), static_cast<SHORT>(csbi.dwSize.Y - (ScrY + 1) - 1) };
		const COORD DestinationOigin{ 0, static_cast<SHORT>(-static_cast<SHORT>(NumLines)) };
		return ScrollScreenBuffer(ScrollRectangle, nullptr, DestinationOigin, Fill) != FALSE;
	}

	bool console::IsViewportVisible() const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi))
			return false;

		const auto Height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
		const auto Width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

		return csbi.srWindow.Bottom >= csbi.dwSize.Y - Height && csbi.srWindow.Left < Width;
	}

	bool console::IsViewportShifted() const
	{
		if (!sWindowMode)
			return false;

		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi))
			return false;

		return csbi.srWindow.Left || csbi.srWindow.Bottom + 1 != csbi.dwSize.Y;
	}

	bool console::IsPositionVisible(point const Position) const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &csbi))
			return false;

		if (!in_range(csbi.srWindow.Left, Position.x, csbi.srWindow.Right))
			return false;

		const auto RealY = Position.y + (sWindowMode? ::GetDelta(csbi) : 0);
		return in_range(csbi.srWindow.Top, RealY, csbi.srWindow.Bottom);
	}

	bool console::GetPalette(std::array<COLORREF, 16>& Palette) const
	{
		CONSOLE_SCREEN_BUFFER_INFOEX csbi{ sizeof(csbi) };
		if (!imports.GetConsoleScreenBufferInfoEx(GetOutputHandle(), &csbi))
			return false;

		std::copy(ALL_CONST_RANGE(csbi.ColorTable), Palette.begin());

		return true;
	}

	void console::EnableWindowMode(bool const Value)
	{
		sWindowMode = Value;
	}

	void console::EnableVirtualTerminal(bool const Value)
	{
		sEnableVirtualTerminal = Value;
	}

	bool console::GetCursorRealPosition(COORD& Position) const
	{
		CONSOLE_SCREEN_BUFFER_INFO ConsoleScreenBufferInfo;
		if (!GetConsoleScreenBufferInfo(GetOutputHandle(), &ConsoleScreenBufferInfo))
			return false;

		Position = ConsoleScreenBufferInfo.dwCursorPosition;
		return true;
	}

	bool console::SetCursorRealPosition(COORD Position) const
	{
		return SetConsoleCursorPosition(GetOutputHandle(), Position) != FALSE;
	}
}

NIFTY_DEFINE(console_detail::console, console);

enum
{
	BufferSize = 10240
};

class consolebuf : public std::wstreambuf
{
public:
	NONCOPYABLE(consolebuf);

	consolebuf():
		m_InBuffer(BufferSize),
		m_OutBuffer(BufferSize)
	{
		setg(m_InBuffer.data(), m_InBuffer.data() + m_InBuffer.size(), m_InBuffer.data() + m_InBuffer.size());
		setp(m_OutBuffer.data(), m_OutBuffer.data() + m_OutBuffer.size());
	}

	void color(const FarColor& Color)
	{
		m_Colour = Color;
	}

protected:
	int_type underflow() override
	{
		size_t Read;
		if (!console.Read(m_InBuffer, Read))
			throw MAKE_FAR_FATAL_EXCEPTION(L"Console read error"sv);

		if (!Read)
			return traits_type::eof();

		setg(m_InBuffer.data(), m_InBuffer.data(), m_InBuffer.data() + Read);
		return m_InBuffer[0];
	}

	int_type overflow(int_type Ch) override
	{
		if (!Write({ pbase(), static_cast<size_t>(pptr() - pbase()) }))
			return traits_type::eof();

		setp(m_OutBuffer.data(), m_OutBuffer.data() + m_OutBuffer.size());

		if (traits_type::eq_int_type(Ch, traits_type::eof()))
		{
			console.Commit();
		}
		else
		{
			sputc(Ch);
		}

		return 0;
	}

	int sync() override
	{
		overflow(traits_type::eof());
		return 0;
	}

private:
	bool Write(string_view Str)
	{
		if (Str.empty())
			return true;

		FarColor CurrentColor;
		const auto ChangeColour = m_Colour && console.GetTextAttributes(CurrentColor);

		if (ChangeColour)
		{
			console.SetTextAttributes(colors::merge(CurrentColor, *m_Colour));
		}

		SCOPE_EXIT{ if (ChangeColour) console.SetTextAttributes(CurrentColor); };

		return console.Write(Str);
	}

	std::vector<wchar_t> m_InBuffer, m_OutBuffer;
	std::optional<FarColor> m_Colour;
};

class console_detail::console::stream_buffers_overrider
{
public:
	NONCOPYABLE(stream_buffers_overrider);

	stream_buffers_overrider():
		m_In(std::wcin, m_BufIn),
		m_Out(std::wcout, m_BufOut),
		m_Err(std::wcerr, m_BufErr),
		m_Log(std::wclog, m_BufLog)
	{
		auto Color = colors::ConsoleColorToFarColor(F_LIGHTRED);
		colors::make_transparent(Color.BackgroundColor);
		m_BufErr.color(Color);
	}

private:
	consolebuf m_BufIn, m_BufOut, m_BufErr, m_BufLog;
	io::wstreambuf_override m_In, m_Out, m_Err, m_Log;
};
