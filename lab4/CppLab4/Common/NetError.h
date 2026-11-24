#pragma once

#include "ConnectionTypes.h"

#include <string>

std::wstring DescribeWinsockError(int code);
std::wstring DescribeFault(ConnectionFault fault, int code);
