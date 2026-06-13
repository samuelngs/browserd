#ifndef BROWSERD_USER_DATA_DIR_H_
#define BROWSERD_USER_DATA_DIR_H_

#include "base/files/file_path.h"

namespace browserd {

bool GetGuiUserDataDir(base::FilePath* path);
bool GetHeadlessUserDataDir(base::FilePath* path);

}  // namespace browserd

#endif  // BROWSERD_USER_DATA_DIR_H_
