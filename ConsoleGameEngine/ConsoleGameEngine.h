#pragma once

#pragma comment(lib, "winmm.lib")

#include <Windows.h>
#include <string>
#include <queue>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <list>

#define DEFAULT_COLOR 0xF0
#define DEFAULT_CHAR ' '

#define MAX_VOLUME 0xFFFF

class ConsoleGameEngine
{
	///////////////////////////////////////// CORE /////////////////////////////////////////////////

private:
	HANDLE console, consoleInput;
	DWORD originalInput;

	SMALL_RECT screenArea;

	CHAR_INFO *screen;

	int screenWidth, screenHeight;

	static std::atomic<bool> running;
	static std::condition_variable finished;
	static std::mutex gameMutex;

public:
	ConsoleGameEngine()
	{
		SecureZeroMemory(keys, sizeof(KeyState) * 256);
	}

	~ConsoleGameEngine() {}

	bool ConstructScreen(int width, int height, int pixelWidth, int pixelHeight)
	{
		if (width < 1 || height < 1)
			return Error(L"Console dimensions must be greater than zero.\n");

		screenWidth = width;
		screenHeight = height;

		//Acquire needed handles
		console = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
		if (console == INVALID_HANDLE_VALUE)
			return Error(L"CreateConsoleScreenBuffer");

		consoleInput = GetStdHandle(STD_INPUT_HANDLE);
		if (consoleInput == INVALID_HANDLE_VALUE)
			return Error(L"GetStdHandle");

		//Temporarily shrink the screen size to minimum
		screenArea = { 0, 0, 1, 1 };
		if (!SetConsoleWindowInfo(console, TRUE, &screenArea))
			return Error(L"SetConsoleWindowInfo");

		if (!SetConsoleTitle(L"Console Game Engine"))
			return Error(L"SetConsoleTitle");

		if (!SetConsoleScreenBufferSize(console, { (short)screenWidth, (short)screenHeight }))
			return Error(L"SetConsoleScreenBufferSize");

		//Remove cursor
		CONSOLE_CURSOR_INFO cursor = { 100, false };
		if (!SetConsoleCursorInfo(console, &cursor))
			return Error(L"SetConsoleCursorInfo");

		//Set font properties (includes size of a text cell)
		CONSOLE_FONT_INFOEX font = { sizeof(font), 0, { (short)pixelWidth, (short)pixelHeight }, FF_DONTCARE, FW_NORMAL };
		wcscpy_s(font.FaceName, L"Consolas");
		if (!SetCurrentConsoleFontEx(console, false, &font))
			return Error(L"SetCurrentConsoleFontEx");

		if (!SetConsoleActiveScreenBuffer(console))
			return Error(L"SetConsoleActiveScreenBuffer");

		//Determine if console of the specified size may be constructed
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		if (!GetConsoleScreenBufferInfo(console, &consoleInfo))
			return Error(L"GetConsoleScreenBufferInfo");

		COORD maxSize = consoleInfo.dwMaximumWindowSize;
		if (screenWidth > maxSize.X || screenHeight > maxSize.Y)
			return Error(L"Specified size and font are too big, impossible to construct the console");

		//Scale the screen size up to the provided dimensions
		screenArea = { 0, 0, (short)screenWidth - 1, (short)screenHeight - 1 };
		if (!SetConsoleWindowInfo(console, TRUE, &screenArea))
			return Error(L"Specified size and font are too big, impossible to construct the console.");

		//Save the original input mode to restore it later on
		if (!GetConsoleMode(consoleInput, &originalInput))
			return Error(L"GetConsoleMode");

		//Set the new input mode
		if (!SetConsoleMode(consoleInput, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT))
			return Error(L"SetConsoleMode");

		//Allocate memory for the screen buffer
		screen = new CHAR_INFO[screenWidth * screenHeight];
		SecureZeroMemory(screen, sizeof(CHAR_INFO) * screenWidth * screenHeight);

		//Set a routine for application's close signal
		SetConsoleCtrlHandler((PHANDLER_ROUTINE)CloseHandler, TRUE);

		//Disable window resizing
		HWND consoleWindow = GetConsoleWindow();
		SetWindowLong(consoleWindow, GWL_STYLE, GetWindowLong(consoleWindow, GWL_STYLE) & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX);

		return true;
	}

private:
	int Error(const wchar_t* message)
	{
		wchar_t lastError[256];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), lastError, 256, NULL);
		SetConsoleMode(consoleInput, originalInput);
		CloseHandle(console);
		wprintf(L"ERROR: %s\n\t%s\n", message, lastError);
		return 0;
	}

public:
	void Start()
	{
		running = true;

		std::thread gameThread = std::thread(&ConsoleGameEngine::GameThread, this);
		gameThread.join();
	}

private:
	void GameThread()
	{
		if (!OnStart())
			running = false;

		auto t1 = std::chrono::system_clock::now();
		auto t2 = std::chrono::system_clock::now();

		while (running)
		{
			//Find time difference between current and previous frames
			t2 = std::chrono::system_clock::now();
			std::chrono::duration<float> duration = t2 - t1;
			float elapsedTime = duration.count();
			t1 = t2;

			ReadInput();

			if (!OnUpdate(elapsedTime))
				running = false;

			WriteConsoleOutput(console, screen, { (short)screenWidth, (short)screenHeight }, { 0, 0 }, &screenArea);

			//If game is finished, check whether it's allowed to exit or not and perform clean-up
			if (!running)
			{
				if (OnDestroy())
				{
					DestroyAudio();
					for (auto &clip : audioClips)
						delete[] clip.data;

					delete[] screen;
					SetConsoleMode(consoleInput, originalInput);
					CloseHandle(console);
					finished.notify_one();
				}
				else
					running = true;
			}
		}
	}

	static BOOL CloseHandler(DWORD evt)
	{
		if (evt == CTRL_CLOSE_EVENT)
		{
			running = false;

			std::unique_lock<std::mutex> lock(gameMutex);
			finished.wait(lock);
		}
		return true;
	}

protected:
	int GetScreenWidth()
	{
		return screenWidth;
	}

	int GetScreenHeight()
	{
		return screenHeight;
	}

	short GetScreenCharacter(int i)
	{
		return screen[i].Char.UnicodeChar;
	}

	short GetScreenCharacter(int x, int y)
	{
		return screen[screenWidth * y + x].Char.UnicodeChar;
	}

	short GetScreenColor(int i)
	{
		return screen[i].Attributes;
	}

	short GetScreenColor(int x, int y)
	{
		return screen[screenWidth * y + x].Attributes;
	}

	void SetApplicationTitle(LPCWSTR title)
	{
		SetConsoleTitle(title);
	}

	virtual bool OnStart() { return true; }

	virtual bool OnUpdate(float elapsedTime) = 0;

	virtual bool OnDestroy() { return true; }

	//////////////////////////////////////// RENDER ////////////////////////////////////////////////

public:
	enum Color
	{
		FG_BLACK = 0x00,
		FG_DARK_BLUE = 0x01,
		FG_DARK_GREEN = 0x02,
		FG_DARK_CYAN = 0x03,
		FG_DARK_RED = 0x04,
		FG_DARK_PINK = 0x05,
		FG_DARK_YELLOW = 0x06,
		FG_GRAY = 0x07,
		FG_DARK_GRAY = 0x08,
		FG_BLUE = 0x09,
		FG_GREEN = 0xA,
		FG_CYAN = 0xB,
		FG_RED = 0xC,
		FG_PINK = 0xD,
		FG_YELLOW = 0xE,
		FG_WHITE = 0xF,

		BG_BLACK = 0x00,
		BG_DARK_BLUE = 0x10,
		BG_DARK_GREEN = 0x20,
		BG_DARK_CYAN = 0x30,
		BG_DARK_RED = 0x40,
		BG_DARK_PINK = 0x50,
		BG_DARK_YELLOW = 0x60,
		BG_GRAY = 0x70,
		BG_DARK_GRAY = 0x80,
		BG_BLUE = 0x90,
		BG_GREEN = 0xA0,
		BG_CYAN = 0xB0,
		BG_RED = 0xC0,
		BG_PINK = 0xD0,
		BG_YELLOW = 0xE0,
		BG_WHITE = 0xF0
	};

	enum PIXEL_TYPE
	{
		PIXEL_QUARTER = 0x2591,
		PIXEL_HALF = 0x2592,
		PIXEL_THREEQUARTERS = 0x2593,
		PIXEL_SOLID = 0x2588
	};

	class Sprite
	{
	private:
		int width = 0;
		int height = 0;

		CHAR_INFO *contents = nullptr;

	public:
		Sprite() {}

		Sprite(int width, int height)
		{
			Create(width, height);
		}

		Sprite(std::wstring fileName)
		{
			if (!Load(fileName))
				Create(8, 8);
		}

		~Sprite()
		{
			delete[] contents;
		}

		int GetWidth()
		{
			return width;
		}

		int GetHeight()
		{
			return height;
		}

		bool Create(int width, int height)
		{
			if (width <= 0 || height <= 0) return false;

			this->width = width;
			this->height = height;

			delete[] contents;
			contents = new CHAR_INFO[width * height];

			for (int i = 0; i < width * height; i++)
			{
				contents[i].Char.UnicodeChar = ' ';
				contents[i].Attributes = BG_BLACK;
			}

			return true;
		}

		void Copy(const Sprite& sprite)
		{
			Create(sprite.width, sprite.height);

			for (int i = 0; i < width; i++)
			{
				for (int j = 0; j < height; j++)
				{
					SetCharacter(i, j, sprite.GetCharacter(i, j));
					SetColor(i, j, sprite.GetColor(i, j));
				}
			}
		}

		void SetCharacter(int x, int y, short character)
		{
			if (x < 0 || x >= width || y < 0 || y >= height) return;
			contents[width * y + x].Char.UnicodeChar = character;
		}

		short GetCharacter(int x, int y)
		{
			if (x < 0 || x >= width || y < 0 || y >= height) return ' ';
			return contents[width * y + x].Char.UnicodeChar;
		}

		const short GetCharacter(int x, int y) const
		{
			if (x < 0 || x >= width || y < 0 || y >= height) return ' ';
			return contents[width * y + x].Char.UnicodeChar;
		}

		void SetColor(int x, int y, short color)
		{
			if (x < 0 || x >= width || y < 0 || y >= height) return;
			contents[width * y + x].Attributes = color;
		}

		short GetColor(int x, int y)
		{
			if (x < 0 || x >= width || y < 0 || y >= height) return BG_BLACK;
			return contents[width * y + x].Attributes;
		}

		const short GetColor(int x, int y) const
		{
			if (x < 0 || x >= width || y < 0 || y >= height) return BG_BLACK;
			return contents[width * y + x].Attributes;
		}

		short SampleCharacter(float x, float y)
		{
			int sx = (int)(x * (float)width);
			int sy = (int)(y * (float)height);

			if (sx < 0)
				sx = 0;
			else if (sx >= width)
				sx = width - 1;

			if (sy < 0)
				sy = 0;
			else if (sy >= height)
				sy = height - 1;

			return contents[width * sy + sx].Char.UnicodeChar;
		}

		short SampleColor(float x, float y)
		{
			int sx = (int)(x * (float)width);
			int sy = (int)(y * (float)height);

			if (sx < 0)
				sx = 0;
			else if (sx >= width)
				sx = width - 1;

			if (sy < 0)
				sy = 0;
			else if (sy >= height)
				sy = height - 1;

			return contents[width * sy + sx].Attributes;
		}

		bool Save(std::wstring fileName)
		{
			FILE *file = nullptr;
			if (_wfopen_s(&file, fileName.c_str(), L"wb") != 0) return false;

			fwrite(&width, sizeof(int), 1, file);
			fwrite(&height, sizeof(int), 1, file);
			fwrite(contents, sizeof(CHAR_INFO), width * height, file);

			fclose(file);

			return true;
		}

		bool Load(std::wstring fileName)
		{
			width = 0;
			height = 0;
			delete[] contents;

			FILE *file = nullptr;
			if (_wfopen_s(&file, fileName.c_str(), L"rb") != 0) return false;

			fread(&width, sizeof(int), 1, file);
			fread(&height, sizeof(int), 1, file);

			Create(width, height);

			fread(contents, sizeof(CHAR_INFO), width * height, file);

			fclose(file);

			return true;
		}

	};

protected:
	void Draw(int index, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		if (index >= 0 || index < screenWidth * screenHeight)
		{
			screen[index].Char.UnicodeChar = character;
			screen[index].Attributes = color;
		}
	}

	void DrawPoint(int x, int y, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		if (x >= 0 && x < screenWidth && y >= 0 && y < screenHeight)
		{
			int index = screenWidth * y + x;
			screen[index].Char.UnicodeChar = character;
			screen[index].Attributes = color;
		}
	}

	void DrawLine(int x0, int y0, int x1, int y1, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		int dx = abs(x1 - x0);
		int sx = (x0 < x1) ? 1 : -1;
		int dy = -abs(y1 - y0);
		int sy = (y0 < y1) ? 1 : -1;
		int error = dx + dy;
		while (true)
		{
			DrawPoint(x0, y0, character, color);
			if (x0 == x1 && y0 == y1)
				break;
			int e2 = error * 2;
			if (e2 >= dy)
			{
				error += dy;
				x0 += sx;
			}
			if (e2 <= dx)
			{
				error += dx;
				y0 += sy;
			}
		}
	}

	void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		DrawLine(x0, y0, x1, y1, character, color);
		DrawLine(x1, y1, x2, y2, character, color);
		DrawLine(x2, y2, x0, y0, character, color);
	}

	void DrawFilledTriangle(int x0, int y0, int x1, int y1, int x2, int y2, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		if (y1 < y0)
		{
			std::swap(y1, y0);
			std::swap(x1, x0);
		}
		if (y2 < y0)
		{
			std::swap(y2, y0);
			std::swap(x2, x0);
		}
		if (y2 < y1)
		{
			std::swap(y2, y1);
			std::swap(x2, x1);
		}

		if (y1 == y2)
		{
			FillBottomFlatTriangle(x0, y0, x1, y1, x2, y2, character, color);
		}
		else if (y0 == y1)
		{
			FillTopFlatTriangle(x0, y0, x1, y1, x2, y2, character, color);
		}
		else
		{
			int x3 = (int)(x0 + ((float)(y1 - y0) / (float)(y2 - y0)) * (x2 - x0) + 0.5f);
			int y3 = y1;
			FillBottomFlatTriangle(x0, y0, x1, y1, x3, y3, character, color);
			FillTopFlatTriangle(x1, y1, x3, y3, x2, y2, character, color);
		}
		DrawLine(x0, y0, x1, y1, character, color);
		DrawLine(x1, y1, x2, y2, character, color);
		DrawLine(x2, y2, x0, y0, character, color);
	}

private:
	void FillBottomFlatTriangle(int x0, int y0, int x1, int y1, int x2, int y2, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		float invSlopeLeft = (float)(x1 - x0) / (float)(y1 - y0);
		float invSlopeRight = (float)(x2 - x0) / (float)(y2 - y0);

		float leftX = (float)x0;
		float rightX = (float)x0;

		for (int y = y0; y <= y1; y++)
		{
			DrawLine((int)(leftX + 0.5f), y, (int)(rightX + 0.5f), y, character, color);
			leftX += invSlopeLeft;
			rightX += invSlopeRight;
		}
	}

	void FillTopFlatTriangle(int x0, int y0, int x1, int y1, int x2, int y2, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		float invSlopeLeft = (float)(x2 - x0) / (float)(y2 - y0);
		float invSlopeRight = (float)(x2 - x1) / (float)(y2 - y1);

		float leftX = (float)x2;
		float rightX = (float)x2;

		for (int y = y2; y > y0; y--)
		{
			DrawLine((int)(leftX + 0.5f), y, (int)(rightX + 0.5f), y, character, color);
			leftX -= invSlopeLeft;
			rightX -= invSlopeRight;
		}
	}

protected:
	void DrawRectangle(int x0, int y0, int x1, int y1, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		DrawLine(x0, y0, x1, y0, character, color);
		DrawLine(x0, y1, x1, y1, character, color);
		DrawLine(x0, y0, x0, y1, character, color);
		DrawLine(x1, y0, x1, y1, character, color);
	}

	void DrawFilledRectangle(int x0, int y0, int x1, int y1, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		if (y1 < y0)
		{
			std::swap(y1, y0);
			std::swap(x1, x0);
		}
		for (int y = y0; y <= y1; y++)
		{
			DrawLine(x0, y, x1, y, character, color);
		}
	}

	void DrawCircle(int cx, int cy, int r, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		int x = r;
		int y = 0;
		int sx = 1 - (2 * r);
		int sy = 1;
		int error = 0;
		while (x >= y)
		{
			DrawPoint(cx + x, cy + y, character, color); //Octant 1
			DrawPoint(cx - x, cy + y, character, color); //Octant 4
			DrawPoint(cx - x, cy - y, character, color); //Octant 5
			DrawPoint(cx + x, cy - y, character, color); //Octant 8
			DrawPoint(cx + y, cy + x, character, color); //Octant 2
			DrawPoint(cx - y, cy + x, character, color); //Octant 3
			DrawPoint(cx - y, cy - x, character, color); //Octant 6
			DrawPoint(cx + y, cy - x, character, color); //Octant 7

			y++;
			error += sy;
			sy += 2;
			if (2 * error + sx > 0)
			{
				x--;
				error += sx;
				sx += 2;
			}
		}
	}

	void DrawFilledCircle(int cx, int cy, int r, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		int x = r;
		int y = 0;
		int sx = 1 - (2 * r);
		int sy = 1;
		int error = 0;
		while (x >= y)
		{
			DrawLine(cx + x, cy + y, cx - x, cy + y, character, color);	//Octants 1 and 4
			DrawLine(cx - x, cy - y, cx + x, cy - y, character, color);	//Octants 5 and 8
			DrawLine(cx + y, cy + x, cx - y, cy + x, character, color);	//Octants 2 and 3
			DrawLine(cx - y, cy - x, cx + y, cy - x, character, color); //Octants 6 and 7

			y++;
			error += sy;
			sy += 2;
			if (2 * error + sx > 0)
			{
				x--;
				error += sx;
				sx += 2;
			}
		}
	}

	void DrawSprite(int x, int y, Sprite& sprite)
	{
		for (int i = 0; i < sprite.GetWidth(); i++)
		{
			for (int j = 0; j < sprite.GetHeight(); j++)
			{
				DrawPoint(x + i, y + j, sprite.GetCharacter(i, j), sprite.GetColor(i, j));
			}
		}
	}

	void DrawSpriteAlpha(int x, int y, Sprite& sprite, short transparencyCol)
	{
		for (int i = 0; i < sprite.GetWidth(); i++)
		{
			for (int j = 0; j < sprite.GetHeight(); j++)
			{
				if (sprite.GetColor(i, j) != transparencyCol)
				{
					DrawPoint(x + i, y + j, sprite.GetCharacter(i, j), sprite.GetColor(i, j));
				}
			}
		}
	}

	void DrawPartialSprite(int x, int y, Sprite& sprite, int ox, int oy, int w, int h)
	{
		for (int i = 0; i < w; i++)
		{
			for (int j = 0; j < h; j++)
			{
				DrawPoint(x + i, y + j, sprite.GetCharacter(i + ox, j + oy), sprite.GetColor(i + ox, j + oy));
			}
		}
	}

	void DrawPartialSpriteAlpha(int x, int y, Sprite& sprite, int ox, int oy, int w, int h, short transparencyCol)
	{
		for (int i = 0; i < w; i++)
		{
			for (int j = 0; j < h; j++)
			{
				if (sprite.GetColor(i + ox, j + oy) != transparencyCol)
				{
					DrawPoint(x + i, y + j, sprite.GetCharacter(i + ox, j + oy), sprite.GetColor(i + ox, j + oy));
				}
			}
		}
	}

	void DisplayText(int x, int y, std::wstring text, short bgColor = DEFAULT_COLOR, short fgColor = FG_BLACK)
	{
		short index = GetScreenWidth() * y + x;
		for (size_t i = 0; i < text.size(); i++)
		{
			Draw(index + i, text[i], bgColor | fgColor);
		}
	}

	void DisplayTextAlpha(int x, int y, std::wstring text, short bgColor = DEFAULT_COLOR, short fgColor = FG_BLACK)
	{
		short index = GetScreenWidth() * y + x;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] != L' ')
				Draw(index + i, text[i], bgColor | fgColor);
		}
	}

	void Fill(int x, int y, int length, short character = DEFAULT_CHAR, short color = DEFAULT_COLOR)
	{
		int index = screenWidth * y + x;
		for (int i = 0; i < length; i++)
		{
			Draw(index + i, character, color);
		}
	}

	void ClearScreen(short character = DEFAULT_CHAR, short color = BG_BLACK)
	{
		Fill(0, 0, screenWidth * screenHeight, character, color);
	}

	void FloodFill(int x, int y, short color = DEFAULT_COLOR)
	{
		int screenSize = screenWidth * screenHeight;
		int index = screenWidth * y + x;
		short targetColor = screen[index].Attributes;

		if (targetColor == color) return;

		screen[index].Attributes = color;

		std::queue<int> nodes;
		nodes.push(index);
		while (!nodes.empty())
		{
			int n = nodes.front();
			nodes.pop();
			if (n + 1 < screenSize && screen[n + 1].Attributes == targetColor)
			{
				screen[n + 1].Attributes = color;
				nodes.push(n + 1);
			}
			if (n - 1 >= 0 && screen[n - 1].Attributes == targetColor)
			{
				screen[n - 1].Attributes = color;
				nodes.push(n - 1);
			}
			if (n + screenWidth < screenSize && screen[n + screenWidth].Attributes == targetColor)
			{
				screen[n + screenWidth].Attributes = color;
				nodes.push(n + screenWidth);
			}
			if (n - screenWidth >= 0 && screen[n - screenWidth].Attributes == targetColor)
			{
				screen[n - screenWidth].Attributes = color;
				nodes.push(n - screenWidth);
			}
		}
	}

	void Clip(int &x, int &y)
	{
		if (x < 0)
			x = 0;
		else if (x > screenWidth)
			x = screenWidth;

		if (y < 0)
			y = 0;
		else if (y > screenHeight)
			y = screenHeight;
	}

	//////////////////////////////////////// INPUT /////////////////////////////////////////////////

private:
	struct KeyState {
		bool pressed = false;
		bool held = false;
		bool released = false;
	} keys[256];

	short mouseX = 0;
	short mouseY = 0;

	void ReadInput()
	{
		for (int i = 0; i < 256; i++)
		{
			keys[i].pressed = false;
			keys[i].released = false;

			if (GetAsyncKeyState(i) & 0x8000)
			{
				keys[i].pressed = !keys[i].held;
				keys[i].held = true;
			}
			else if (keys[i].held)
			{
				keys[i].released = true;
				keys[i].held = false;
			}
		}

		INPUT_RECORD inputBuffer[32];
		DWORD events = 0;
		GetNumberOfConsoleInputEvents(consoleInput, &events);
		if (events > 0)
			ReadConsoleInput(consoleInput, inputBuffer, events, &events);

		for (DWORD i = 0; i < events; i++)
		{
			switch (inputBuffer[i].EventType)
			{
				case MOUSE_EVENT:
				{
					switch (inputBuffer[i].Event.MouseEvent.dwEventFlags)
					{
						case MOUSE_MOVED:
						{
							mouseX = inputBuffer[i].Event.MouseEvent.dwMousePosition.X;
							mouseY = inputBuffer[i].Event.MouseEvent.dwMousePosition.Y;
							break;
						}
					}
					break;
				}
			}
		}

	}

protected:
	short GetMouseX()
	{
		return mouseX;
	}

	short GetMouseY()
	{
		return mouseY;
	}

	KeyState GetKey(short key)
	{
		return keys[key];
	}

	//////////////////////////////////////// AUDIO /////////////////////////////////////////////////

private:
	class AudioClip
	{
	public:
		WAVEFORMATEX format;
		float *data = nullptr;

		long length;

		bool isValid = false;

		AudioClip() {}

		~AudioClip() {}

		AudioClip(std::wstring fileName)
		{
			FILE *file = nullptr;
			if (_wfopen_s(&file, fileName.c_str(), L"rb") != 0) return;

			//The majority of header fields take 4 bytes of data
			char field[4];

			//Read the "RIFF" header
			std::fread(&field, sizeof(char), 4, file);
			if (strncmp(field, "RIFF", 4) != 0)
			{
				std::fclose(file);
				return;
			}

			//Read size of the chunk; ignore it
			std::fread(&field, sizeof(char), 4, file);

			//Read the "WAVE" header
			std::fread(&field, sizeof(char), 4, file);
			if (strncmp(field, "WAVE", 4) != 0)
			{
				std::fclose(file);
				return;
			}

			//Read the "fmt " header (space included)
			std::fread(&field, sizeof(char), 4, file);
			if (strncmp(field, "fmt ", 4) != 0)
			{
				std::fclose(file);
				return;
			}

			//Read size of the subchunk; ignore it
			std::fread(&field, sizeof(char), 4, file);

			//Read audio file format. WAVEFORMATEX struct takes 2 additional bytes of data that can't be found in .wav files, so we ignore them
			std::fread(&format, sizeof(WAVEFORMATEX) - 2, 1, file);

			//Only specific audio format is currently supported
			if (format.wBitsPerSample != 16 || format.nSamplesPerSec != 44100)
			{
				std::fclose(file);
				return;
			}

			//Read next subchunk's header and size (this time the size is actually needed)
			long subchunkSize = 0;
			std::fread(&field, sizeof(char), 4, file);
			std::fread(&subchunkSize, sizeof(long), 1, file);

			//Search for "data" subchunk. In some cases .wav files may have additional subchunks, so we need to make sure we found the correct one
			while (strncmp(field, "data", 4) != 0)
			{
				std::fseek(file, subchunkSize, SEEK_CUR);
				std::fread(&field, sizeof(char), 4, file);
				std::fread(&subchunkSize, sizeof(long), 1, file);
			}

			length = subchunkSize / (format.nChannels * (format.wBitsPerSample / 8));

			data = new float[length * format.nChannels];
			float *currentSample = data;

			//Read the data into the buffer
			for (long i = 0; i < length; i++)
			{
				for (int c = 0; c < format.nChannels; c++)
				{
					short sample = 0;
					std::fread(&sample, sizeof(short), 1, file);
					*currentSample = (float)sample / (float)MAXSHORT;	//Normalize it to prevent integer overflow later on
					currentSample++;
				}
			}

			std::fclose(file);
			isValid = true;
		}

	};

	struct CurrentlyPlayingClip
	{
		int audioClipID = 0;

		long samplePosition = 0;

		bool looped = false;
		bool paused = false;
		bool finished = false;

		CurrentlyPlayingClip() {}

		CurrentlyPlayingClip(int id, bool looped) : audioClipID(id), looped(looped) {}

		void Restart()
		{
			samplePosition = 0;
			paused = false;
			finished = false;
		}
	};

	std::vector<AudioClip> audioClips;
	std::list<CurrentlyPlayingClip> currentlyPlayingClips;

	HWAVEOUT device = 0;

	int samplesPerSec = 0;
	int channels = 0;
	int blockCount = 0;
	int samplesPerBlock = 0;

	short *samplesMemory = nullptr;
	WAVEHDR *blocks = nullptr;

	int currentBlock = 0;
	std::atomic<int> freeBlocks = 0;

	std::mutex writingBlock;
	std::condition_variable blockWritten;

	std::atomic<bool> audioThreadActive = false;
	std::thread audioThread;

	std::atomic<float> globalTime = 0.0f;

	bool soundMuted = false;
	int currentVolume = MAX_VOLUME;

	//Static wrapper for the callback function because Microsoft
	//(callback cannot be method of the class itself, but it may be called via dwInstance)
	static void CALLBACK waveOutProcWrapper(HWAVEOUT hwo, UINT uMsg, DWORD dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
	{
		((ConsoleGameEngine*)dwInstance)->waveOutProc(hwo, uMsg, dwParam1, dwParam2);
	}

	//The actual callback function
	void waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
	{
		if (uMsg == WOM_DONE)
		{
			freeBlocks++;
			std::unique_lock<std::mutex> lock(writingBlock);
			blockWritten.notify_one();
		}
	}

	void AudioThread()
	{
		float timeStep = 1.0f / (float)samplesPerSec;

		auto clip = [](float sample, float maxSample)
		{
			if (sample >= 0)
				return min(sample, maxSample);
			else
				return max(sample, -maxSample);
		};

		while (audioThreadActive)
		{
			//Wait for the soundcard to request a block of audio samples
			if (freeBlocks == 0)
			{
				std::unique_lock<std::mutex> lock(writingBlock);
				while (freeBlocks == 0)
					blockWritten.wait(lock);
			}

			//Use the block
			freeBlocks--;

			//Unprepare block if it has already been used
			if (blocks[currentBlock].dwFlags & WHDR_PREPARED)
				waveOutUnprepareHeader(device, &blocks[currentBlock], sizeof(WAVEHDR));

			int sampleOffset = currentBlock * samplesPerBlock;

			for (int i = 0; i < samplesPerBlock; i += channels)
			{
				for (int c = 0; c < channels; c++)
				{
					samplesMemory[sampleOffset + i + c] = (short)(clip(GetMixerOutput(timeStep, c), 1.0f) * (float)MAXSHORT);
				}

				globalTime = globalTime + timeStep;
			}

			waveOutPrepareHeader(device, &blocks[currentBlock], sizeof(WAVEHDR));
			waveOutWrite(device, &blocks[currentBlock], sizeof(WAVEHDR));

			currentBlock++;
			currentBlock %= blockCount;
		}
	}

	float GetMixerOutput(float timeStep, int channel)
	{
		float mixedSample = 0.0f;

		for (auto &clip : currentlyPlayingClips)
		{
			if (clip.paused)
				continue;

			clip.samplePosition += (long)((float)audioClips[clip.audioClipID].format.nSamplesPerSec * timeStep);

			if (clip.samplePosition < audioClips[clip.audioClipID].length)
				mixedSample += audioClips[clip.audioClipID].data[(clip.samplePosition * audioClips[clip.audioClipID].format.nChannels) + channel];
			else if (clip.looped)
				clip.Restart();
			else
				clip.finished = true;
		}

		currentlyPlayingClips.remove_if([](const CurrentlyPlayingClip &clip) { return clip.finished; });

		mixedSample += onUserSoundSample(channel, globalTime, timeStep);

		return onUserSoundFilter(channel, globalTime, mixedSample);
	}

public:
	void PlayAudioClip(int id, bool loop = false)
	{
		if (id < 0 || id >= (int)audioClips.size()) return;
		CurrentlyPlayingClip clipToPlay(id, loop);
		currentlyPlayingClips.push_back(clipToPlay);
	}

protected:
	unsigned int LoadAudioClip(std::wstring fileName)
	{
		AudioClip audioClip(fileName);

		if (!audioClip.isValid)
			return -1;

		audioClips.push_back(audioClip);
		return audioClips.size() - 1;
	}

	bool StartAudio(int samplesPerSec = 44100, int channels = 1, int blockCount = 8, int samplesPerBlock = 512)
	{
		if (audioThreadActive)
			DestroyAudio();

		this->samplesPerSec = samplesPerSec;
		this->channels = channels;
		this->blockCount = blockCount;
		this->samplesPerBlock = samplesPerBlock;

		freeBlocks = blockCount;

		soundMuted = false;
		currentVolume = MAX_VOLUME;

		WAVEFORMATEX format;
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = channels;
		format.nSamplesPerSec = samplesPerSec;
		format.wBitsPerSample = sizeof(short) * 8;
		format.nBlockAlign = format.nChannels * (format.wBitsPerSample / 8);
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		format.cbSize = 0;

		if (waveOutOpen(&device, WAVE_MAPPER, &format, (DWORD_PTR)waveOutProcWrapper, (DWORD_PTR)this, CALLBACK_FUNCTION) != S_OK)
			return false;

		samplesMemory = new short[blockCount * samplesPerBlock];
		if (samplesMemory == nullptr)
			return false;
		SecureZeroMemory(samplesMemory, sizeof(short) * blockCount * samplesPerBlock);

		blocks = new WAVEHDR[blockCount];
		if (blocks == nullptr)
			return false;
		SecureZeroMemory(blocks, sizeof(WAVEHDR) * blockCount);

		//Make each block point to a particular position in the buffer of samples
		for (int i = 0; i < blockCount; i++)
		{
			blocks[i].lpData = (LPSTR)(samplesMemory + (i * samplesPerBlock));
			blocks[i].dwBufferLength = sizeof(short) * samplesPerBlock;
		}

		audioThreadActive = true;
		audioThread = std::thread(&ConsoleGameEngine::AudioThread, this);

		std::unique_lock<std::mutex> lock(writingBlock);
		blockWritten.notify_one();

		return true;
	}

	void DestroyAudio()
	{
		audioThreadActive = false;

		if (audioThread.joinable())
			audioThread.join();

		waveOutSetVolume(device, MAX_VOLUME);
		soundMuted = false;

		waveOutReset(device);
		waveOutClose(device);

		samplesPerSec = 0;
		channels = 0;
		blockCount = 0;
		samplesPerBlock = 0;

		globalTime = 0.0f;

		if (samplesMemory != nullptr) delete[] samplesMemory;

		if (blocks != nullptr)
		{
			for (int i = 0; i < blockCount; i++)
				waveOutUnprepareHeader(device, &blocks[i], sizeof(WAVEHDR));

			delete[] blocks;
		}
	}

	virtual float onUserSoundSample(int channel, float globalTime, float timeStep)
	{
		return 0.0f;
	}

	virtual float onUserSoundFilter(int channel, float globalTime, float mixedSample)
	{
		return mixedSample;
	}

	int GetVolume()
	{
		return currentVolume;
	}

	void SetVolume(float percent)
	{
		if (percent < 0)
			percent = 0;
		else if (percent > 100)
			percent = 100;

		int oneChannelVolume = (int)(percent * 2.55f);
		currentVolume = oneChannelVolume * oneChannelVolume;
		if (!soundMuted)
			waveOutSetVolume(device, currentVolume);
	}

	void MuteAudio()
	{
		soundMuted = true;
		waveOutSetVolume(device, 0x0000);
	}

	void UnmuteAudio()
	{
		soundMuted = false;
		waveOutSetVolume(device, currentVolume);
	}

	void PauseAudio(int id)
	{
		for (auto &clip : currentlyPlayingClips)
		{
			if (clip.audioClipID == id)
				clip.paused = !clip.paused;
		}
	}

	void PauseAllAudio()
	{
		for (auto &clip : currentlyPlayingClips)
		{
			clip.paused = !clip.paused;
		}
	}

	void RestartAudio(int id)
	{
		for (auto &clip : currentlyPlayingClips)
		{
			if (clip.audioClipID == id)
				clip.Restart();
		}
	}

	void RestartAllAudio()
	{
		for (auto &clip : currentlyPlayingClips)
		{
			clip.Restart();
		}
	}

	void StopAudio(int id)
	{
		for (auto &clip : currentlyPlayingClips)
		{
			if (clip.audioClipID == id)
				clip.finished = true;
		}
	}

	void StopAllAudio()
	{
		currentlyPlayingClips.clear();
		waveOutReset(device);
	}

};

std::atomic<bool> ConsoleGameEngine::running = false;
std::condition_variable ConsoleGameEngine::finished;
std::mutex ConsoleGameEngine::gameMutex;