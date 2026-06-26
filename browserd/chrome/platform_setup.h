#ifndef BROWSERD_CHROME_PLATFORM_SETUP_H_
#define BROWSERD_CHROME_PLATFORM_SETUP_H_

namespace browserd::chrome {

bool SetUpChromePlatform(int argc, const char** argv);
void ActivateChromeApplication();

}  // namespace browserd::chrome

#endif  // BROWSERD_CHROME_PLATFORM_SETUP_H_
