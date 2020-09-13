#pragma once

#include "Array.h"
#include "String.h"

namespace sf {

struct License
{
	sf::StringBuf name;
	sf::StringBuf type;
	sf::Array<sf::StringBuf> thanks;
	sf::StringBuf text;
};

void getLicenses(sf::Array<sf::License> &licenses);

}
