#include "ConsoleGameEngine.h"

class Demo : public ConsoleGameEngine
{
public:
	bool OnStart() override
	{
		return true;
	}

	bool OnUpdate(float elapsedTime) override
	{
		for (int i = 0; i < GetScreenWidth() * GetScreenHeight(); i++)
		{
			Draw(i, ' ', rand() % 255);
		}
		return true;
	}

};

int main()
{
	Demo demo;
	if (demo.ConstructScreen(128, 64, 8, 8))
		demo.Start();

	return 0;
}