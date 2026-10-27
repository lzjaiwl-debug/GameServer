#pragma once
#include <CtrlLib/CtrlLib.h>
#include <Terminal/Terminal.h>
#include <Core/SSL/SSL.h>
using namespace Upp;
#include <vector>
#include <mutex>
#ifdef _WIN32
#include <winioctl.h>
#elif defined(PLATFORM_POSIX)
#include <fstream>
#include <sstream>
#include <set>
#include <cstdio>
#endif

#define LAYOUTFILE "res/AdminCertification.lay"
#include <CtrlCore/lay.h>

#include "inc/AdminUsbKeyIssuer.h"
#include "inc/AdminLoginCode.h"
#include "inc/AuthPage.h"