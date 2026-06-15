// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Steven Briscoe

#include <exception>
 
class UnsupportedProcessorException: public std::exception
{
	virtual const char* what() const throw()
	{
		return "Unsupported processor";
	}
};
