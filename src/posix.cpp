/*!
 * @file      posix.cpp
 * @brief     POSIX implementation of @ref grading::MapSharedData and
 *            @ref grading::ForkTest..
 *
 * @author    Jonathan Anderson <jonathan.anderson@mun.ca>
 * @copyright (c) 2014-2015 Jonathan Anderson. All rights reserved.
 * @license   Apache License, Version 2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include <libgrading.h>
#include "private.h"

#include <cassert>
#include <functional>

#include <sys/mman.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

using namespace grading;
using namespace std;


CheckResult::~CheckResult()
{
	if (reportError_)
	{
		cerr << "Check failed.\n";

		if (expected_.empty())
			cerr << "  " << actual_ << "\n";

		else
			cerr
				<< "  expected `" << expected_
				<< "`, got `" << actual_ << "`\n"
				;

		cerr
			<< "  " << message_.str()
			<< "\n\n"
			;

		exit(static_cast<int>(TestResult::Fail));
	}
}


static TestResult ProcessChildStatus(int status)
{
	if (WIFEXITED(status))
		return static_cast<TestResult>(WEXITSTATUS(status));

	if (WIFSIGNALED(status))
	{
		switch (WTERMSIG(status))
		{
			case SIGABRT:
			return TestResult::Abort;

			case SIGSEGV:
			return TestResult::Segfault;

			default:
			return TestResult::OtherError;
		}
	}

	assert(false && "unhandled child exit mode");
}



/**
 * @internal @brief A memory-mapped POSIX shared memory segment.
 */
class PosixSharedMemory : public SharedMemory
{
	public:
	PosixSharedMemory(int fd, size_t len, void *rawPtr)
		: shmfd(fd), length(len), ptr(rawPtr)
	{
	}

	~PosixSharedMemory()
	{
		munmap(ptr, length);
		close(shmfd);
	}

	virtual void *rawPointer() const override { return ptr; }

	private:
	int shmfd;
	size_t length;
	void *ptr;
};


unique_ptr<SharedMemory> grading::MapSharedData(size_t len)
{
	int shmfd = shm_open("shm.tmp", 0, O_RDWR);
	void *map = mmap(0, len, PROT_READ | PROT_WRITE,
	                 MAP_ANON | MAP_SHARED, shmfd, 0);

	return unique_ptr<SharedMemory>(new PosixSharedMemory(shmfd, len, map));
}


TestResult grading::ForkTest(TestClosure test, time_t timeout)
{
	std::cout.flush();
	std::cerr.flush();
	std::clog.flush();

	pid_t child = fork();

	if (child == 0)
	{
		TestResult result = RunInProcess(test);
		exit(static_cast<int>(result));
	}
	else
	{
		int status;
		int options = (timeout ? WNOHANG : 0);

		time_t start = time(nullptr);

		while (true)
		{
			pid_t result = waitpid(child, &status, options);

			// Success: the child process has returned.
			if (result == child)
				break;

			// Error in waitpid()?
			if (result < 0)
			{
				assert(errno == EINTR);
				continue;
			}

			// Child process isn't finished yet.
			const time_t now = time(nullptr);
			if ((now - start) > timeout)
			{
				kill(child, SIGKILL);
				waitpid(child, &status, 0);
				return TestResult::Timeout;
			}

			usleep(100);
		}

		return ProcessChildStatus(status);
	}
}


#ifdef __FreeBSD__
#include <sys/capsicum.h>

#include <err.h>
#include <errno.h>
#include <sysexits.h>

void grading::EnterSandbox()
{
	if ((cap_enter() != 0) and (errno != ENOSYS))
		err(EX_OSERR, "Error in cap_enter()");
}
#else

#warning EnterSandbox() does nothing on the current platform
void grading::EnterSandbox()
{
}

#endif
