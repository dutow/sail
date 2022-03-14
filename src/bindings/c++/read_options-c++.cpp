/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

    Copyright (c) 2020 Dmitry Baryshev

    The MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "sail-c++.h"
#include "sail.h"

namespace sail
{

class SAIL_HIDDEN read_options::pimpl
{
public:
    pimpl()
    {
    }

    sail::codec_options codec_options;
};

read_options::read_options()
    : d(new pimpl)
{
}

read_options::read_options(const read_options &ro)
    : read_options()
{
    *this = ro;
}

read_options& read_options::operator=(const sail::read_options &read_options)
{
    with_codec_options(read_options.codec_options());

    return *this;
}

read_options::read_options(sail::read_options &&read_options) noexcept
{
    *this = std::move(read_options);
}

read_options& read_options::operator=(read_options &&ro) noexcept
{
    d = std::move(ro.d);

    return *this;
}

read_options::~read_options()
{
}

sail::codec_options& read_options::codec_options() const
{
    return d->codec_options;
}

read_options& read_options::with_codec_options(const sail::codec_options &codec_options)
{
    d->codec_options = codec_options;

    return *this;
}

read_options::read_options(const sail_read_options *ro)
    : read_options()
{
    if (ro == nullptr) {
        SAIL_LOG_TRACE("NULL pointer has been passed to sail::read_options(). The object is untouched");
        return;
    }

    with_codec_options(utils_private::sail_codec_options_to_codec_options(ro->codec_options));
}

sail_status_t read_options::to_sail_read_options(sail_read_options **read_options) const
{
    SAIL_CHECK_PTR(read_options);

    SAIL_TRY(sail_alloc_read_options(read_options));

    utils_private::codec_options_to_sail_codec_options(d->codec_options, (*read_options)->codec_options);

    return SAIL_OK;
}

}
