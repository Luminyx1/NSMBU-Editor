#ifndef MATH_VEC4_H_
#define MATH_VEC4_H_

#include "math_VEC2.h"
#include "math_VEC3.h"

namespace nw { namespace math {

struct VEC4
{
    f32 MagnitudeSquare() const
    {
        return x*x + y*y + z*z + w*w;
    }

    f32 Magnitude() const
    {
        return sqrtf(MagnitudeSquare());
    }

    void Normalize()
    {
        f32 inv_mag = 1.0f / Magnitude(); // No division-by-zero check...
        x *= inv_mag;
        y *= inv_mag;
        z *= inv_mag;
        w *= inv_mag;
    }

    VEC2& xy()
    {
        return ((VEC2*)this)[0];
    }

    VEC2& zw()
    {
        return ((VEC2*)this)[1];
    }

    VEC3& xyz()
    {
        return *((VEC3*)this);
    }

    const VEC2& xy() const
    {
        return ((const VEC2*)this)[0];
    }

    const VEC2& zw() const
    {
        return ((const VEC2*)this)[1];
    }

    const VEC3& xyz() const
    {
        return *((const VEC3*)this);
    }

    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

inline VEC4& operator+=(VEC4& lhs, const VEC4& rhs)
{
    lhs.x += rhs.x;
    lhs.y += rhs.y;
    lhs.z += rhs.z;
    lhs.w += rhs.w;
    return lhs;
}

inline VEC4& operator-=(VEC4& lhs, const VEC4& rhs)
{
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;
    lhs.z -= rhs.z;
    lhs.w -= rhs.w;
    return lhs;
}

inline VEC4& operator*=(VEC4& lhs, f32 a)
{
    lhs.x *= a;
    lhs.y *= a;
    lhs.z *= a;
    lhs.w *= a;
    return lhs;
}

inline VEC4 operator+(const VEC4& lhs, const VEC4& rhs)
{
    VEC4 ret = lhs;
    return (ret += rhs);
}

inline VEC4 operator-(const VEC4& lhs, const VEC4& rhs)
{
    VEC4 ret = lhs;
    return (ret -= rhs);
}

inline VEC4 operator*(const VEC4& lhs, f32 a)
{
    VEC4 ret = lhs;
    return (ret *= a);
}

inline VEC4 operator*(f32 a, const VEC4& rhs)
{
    return (VEC4){ a * rhs.x, a * rhs.y, a * rhs.z, a * rhs.w };
}

} } // namespace nw::math

#endif // MATH_VEC4_H_
