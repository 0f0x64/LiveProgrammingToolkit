bool isWatchingTextures = false;
HANDLE fileRec = NULL;
OVERLAPPED overlappedRec;
BOOL successRec = false;
const int changeBufLen_rec = 1024 * 4;
uint8_t change_buf_rec[changeBufLen_rec];
#define objPath "textures"

#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

bool IsValidAssetFile(const std::string& fileStr) {
    fs::path p(fileStr);
    std::string filename = p.filename().string();
    std::string ext = p.extension().string();

    // Переводим расширение в нижний регистр для надежности (.PNG -> .png)
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
        });

    // 1. Отсеиваем файлы без расширения
    if (ext.empty()) return false;

    // 2. Отсеиваем временные файлы Photoshop и Windows (~PST123.tmp, ~$file.psd)
    if (filename[0] == '~' || filename[0] == '.') return false;
    if (ext == ".tmp" || ext == ".temp" || ext == ".bak" || ext == ".crdownload") return false;

    return true;
}

void WatchForRecompilation() {
    const std::string targetExt = ".png";

    if (!isWatchingTextures) {
        Log((std::string("watching textures ") + objPath).c_str());
        fileRec = CreateFileA(
            objPath,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL
        );
        assert(fileRec != INVALID_HANDLE_VALUE);
        overlappedRec.hEvent = CreateEvent(NULL, FALSE, 0, NULL);
        successRec = ReadDirectoryChangesW(
            fileRec,
            change_buf_rec,
            changeBufLen_rec,
            TRUE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            NULL,
            &overlappedRec,
            NULL
        );
        isWatchingTextures = true;
    }

    DWORD result = WaitForSingleObject(overlappedRec.hEvent, 0);
    if (result == WAIT_OBJECT_0) {
        DWORD bytes_transferred = 0;
        if (GetOverlappedResult(fileRec, &overlappedRec, &bytes_transferred, FALSE) && bytes_transferred > 0) {
            DWORD offset = 0;

            while (true) {
                FILE_NOTIFY_INFORMATION* event = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(&change_buf_rec[offset]);

                // Ловим и обычное изменение, и переименование из .tmp (Photoshop)
                if (event->Action == FILE_ACTION_MODIFIED || event->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                    char fileName[MAX_PATH] = { 0 };
                    int len = WideCharToMultiByte(
                        CP_ACP, 0,
                        event->FileName, event->FileNameLength / sizeof(WCHAR),
                        fileName, MAX_PATH - 1,
                        NULL, NULL
                    );
                    fileName[len] = '\0';

                    std::string fileStr(fileName);
                    // Проверяем, что имя заканчивается на .png
                    if (IsValidAssetFile(fileStr)) {
                        Log((std::string("updated ") + fileStr).c_str());
                        auto id = getTextureIDfromName(fileStr.c_str());
                        Textures::LoadTexture(id, fileStr.c_str());
                        Textures::TextureToGPU(id);
                        // Перезагружаем ресурс
                    }
                }

                if (event->NextEntryOffset == 0) {
                    break;
                }
                offset += event->NextEntryOffset;
            }
        }

        // Перезапускаем отслеживание
        ReadDirectoryChangesW(
            fileRec,
            change_buf_rec,
            changeBufLen_rec,
            TRUE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            NULL,
            &overlappedRec,
            NULL
        );
    }
}
