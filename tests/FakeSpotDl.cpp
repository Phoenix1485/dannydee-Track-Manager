#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    bool canonicalUrl = argc > 2
        && std::strcmp(argv[1], "download") == 0
        && std::strcmp(argv[2], "https://open.spotify.com/track/test") == 0;
    bool flac = false;
    bool externalAudio = false;
    for (int index = 3; index < argc; ++index) {
        if (std::strcmp(argv[index], "--format") == 0 && index + 1 < argc
            && std::strcmp(argv[index + 1], "flac") == 0) {
            flac = true;
        }
        if (std::strcmp(argv[index], "--audio") == 0 && index + 1 < argc
            && std::strcmp(argv[index + 1], "youtube-music") == 0) {
            externalAudio = true;
        }
    }
    if (!canonicalUrl || !flac || !externalAudio) {
        std::puts("FAKE_SPOTDL_ARGUMENT_ERROR");
        return 7;
    }
    std::puts("FAKE_SPOTDL_EXTERNAL_AUDIO_ENABLED");
    return 1;
}
