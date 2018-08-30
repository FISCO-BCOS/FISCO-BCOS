#pragma once
#include <exception>

namespace dev {

namespace precompiled {

class FatalException: public std::exception {
public:
	FatalException() _GLIBCXX_USE_NOEXCEPT { }

	virtual ~FatalException() _GLIBCXX_USE_NOEXCEPT;

	virtual const char* what() const _GLIBCXX_USE_NOEXCEPT;
};

}
}
