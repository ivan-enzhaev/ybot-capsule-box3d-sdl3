#pragma once

#include <SDL3/SDL.h>
#include <ozz/base/io/stream.h>

class OzzSDLStream : public ozz::io::Stream
{
public:
    OzzSDLStream(SDL_IOStream *io)
        : io_(io)
        , opened_(io != nullptr)
    {
        if (io_)
            SDL_SeekIO(io_, 0, SDL_IO_SEEK_SET);
    }

    ~OzzSDLStream()
    {
        if (io_)
            SDL_CloseIO(io_);
    }

    bool opened() const override { return opened_; }

    size_t Read(void *buffer, size_t size) override
    {
        size_t total = 0;

        while (total < size)
        {
            size_t read = SDL_ReadIO(io_, (uint8_t *)buffer + total, size - total);
            if (read == 0)
                break; // EOF or error

            total += read;
        }

        return total;
    }

    size_t Write(const void *, size_t) override { return 0; }

    int Seek(int offset, Origin origin) override
    {
        SDL_IOWhence whence = SDL_IO_SEEK_SET;
        if (origin == kCurrent)
            whence = SDL_IO_SEEK_CUR;
        else if (origin == kEnd)
            whence = SDL_IO_SEEK_END;

        Sint64 pos = SDL_SeekIO(io_, offset, whence);
        return pos >= 0 ? 0 : -1;
    }

    int Tell() const override
    {
        Sint64 pos = SDL_TellIO(io_);
        return pos >= 0 ? (int)pos : -1;
    }

    size_t Size() const override
    {
        Sint64 size = SDL_GetIOSize(io_);
        return size >= 0 ? (size_t)size : 0;
    }

private:
    SDL_IOStream *io_;
    bool opened_;
};
