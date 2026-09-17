#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    if (argc < 3 || std::strcmp(argv[2], "https://open.spotify.com/track/test") != 0)
        return 7;
    std::puts("Processing query: https://open.spotify.com/track/test");
    std::puts("AudioProviderError: YT-DLP download error - https://www.youtube.com/watch?v=YE7VzlLtp-4");
    return 1;
}
