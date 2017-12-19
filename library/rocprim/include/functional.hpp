// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef ROCPRIM_FUNCTIONAL_HPP_
#define ROCPRIM_FUNCTIONAL_HPP_

#include <functional>

#include "detail/config.hpp"

BEGIN_ROCPRIM_NAMESPACE

template<class T>
inline constexpr T max(const T& a, const T& b) [[hc]] [[cpu]]
{
    return a < b ? b : a;
}

template<class T>
inline constexpr T min(const T& a, const T& b) [[hc]] [[cpu]]
{
    return a < b ? a : b;
}

template<class T>
void swap(T& a, T& b) [[hc]] [[cpu]]
{
    T c = a;
    a = b;
    b = c;
}

template<class T>
struct less : public std::binary_function<T, T, bool>
{
    constexpr bool operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a < b;
    }
};

template<class T>
struct less_equal : public std::binary_function<T, T, bool>
{
    constexpr bool operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a <= b;
    }
};

template<class T>
struct greater : public std::binary_function<T, T, bool>
{
    constexpr bool operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a > b;
    }
};

template<class T>
struct greater_equal : public std::binary_function<T, T, bool>
{
    constexpr bool operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a >= b;
    }
};

template<class T>
struct equal_to : public std::binary_function<T, T, bool>
{
    constexpr bool operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a == b;
    }
};

template<class T>
struct not_equal_to : public std::binary_function<T, T, bool>
{
    constexpr bool operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a != b;
    }
};

template<class T>
struct plus : public std::binary_function<T, T, T>
{
    constexpr T operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a + b;
    }
};

template<class T>
struct minus : public std::binary_function<T, T, T>
{
    constexpr T operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a - b;
    }
};

template<class T>
struct multiplies : public std::binary_function<T, T, T>
{
    constexpr T operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a * b;
    }
};

template<class T>
struct maximum : public std::binary_function<T, T, T>
{
    constexpr T operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a < b ? b : a;
    }
};

template<class T>
struct minimum : public std::binary_function<T, T, T>
{
    constexpr T operator()(const T& a, const T& b) const [[hc]] [[cpu]]
    {
        return a < b ? a : b;
    }
};

END_ROCPRIM_NAMESPACE

#endif // ROCPRIM_FUNCTIONAL_HPP_