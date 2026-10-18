#ifndef CVECTOR_H
#define CVECTOR_H

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>

template <bool DimensionIsRightForThisOperation>
struct CNeeds;

template <>
struct CNeeds<true> { static void check() {} };

template <int M, int N> class CMatrix;

template <int N = 3>
class CVector {
public:
    double scalar[N];

    CVector() {
        for (int i = 0; i < N; ++i) scalar[i] = 0.0;
    }

    explicit CVector(double fill) {
        for (int i = 0; i < N; ++i) scalar[i] = fill;
    }

    CVector(std::initializer_list<double> list) {
        assert(static_cast<int>(list.size()) == N &&
               "CVector: braced list has the wrong number of components");
        int i = 0;
        for (std::initializer_list<double>::const_iterator it = list.begin();
             it != list.end() && i < N; ++it, ++i)
            scalar[i] = *it;
        for (; i < N; ++i) scalar[i] = 0.0;
    }

    CVector(double x, double y) {
        CNeeds<(N == 2)>::check();
        scalar[0] = x;
        scalar[1] = y;
    }

    CVector(double x, double y, double z) {
        CNeeds<(N == 3)>::check();
        scalar[0] = x;
        scalar[1] = y;
        scalar[2] = z;
    }

    CVector(double x, double y, double z, double w) {
        CNeeds<(N == 4)>::check();
        scalar[0] = x;
        scalar[1] = y;
        scalar[2] = z;
        scalar[3] = w;
    }

    explicit CVector(const double (&a)[N]) {
        for (int i = 0; i < N; ++i) scalar[i] = a[i];
    }

    static CVector zero() { return CVector(); }

    static CVector unit(int i) {
        CVector e;
        e.scalar[i] = 1.0;
        return e;
    }

    int size() const { return N; }

    double& operator[](int i)       { return scalar[i]; }
    double  operator[](int i) const { return scalar[i]; }

    CVector& operator+=(const CVector& v) {
        for (int i = 0; i < N; ++i) scalar[i] += v.scalar[i];
        return *this;
    }

    CVector& operator-=(const CVector& v) {
        for (int i = 0; i < N; ++i) scalar[i] -= v.scalar[i];
        return *this;
    }

    CVector& operator*=(double s) {
        for (int i = 0; i < N; ++i) scalar[i] *= s;
        return *this;
    }

    CVector& operator/=(double s) {
        for (int i = 0; i < N; ++i) scalar[i] /= s;
        return *this;
    }

    CVector operator-() const {
        CVector r;
        for (int i = 0; i < N; ++i) r.scalar[i] = -scalar[i];
        return r;
    }

    double dot(const CVector& v) const {
        double s = 0.0;
        for (int i = 0; i < N; ++i) s += scalar[i] * v.scalar[i];
        return s;
    }

    double lengthSquared() const { return dot(*this); }

    double length() const { return std::sqrt(lengthSquared()); }

    CVector normalized() const {
        double len = length();
        if (len == 0.0) return *this;
        return *this / len;
    }

    void normalize() { *this = normalized(); }

    bool isZero(double tol = 0.0) const { return lengthSquared() <= tol * tol; }

    CVector projectedOnto(const CVector& onto) const {
        double d = onto.lengthSquared();
        if (d == 0.0) return CVector();
        return onto * (dot(onto) / d);
    }

    CVector rejectedFrom(const CVector& from) const {
        return *this - projectedOnto(from);
    }

    CVector reflected(const CVector& n) const {
        return *this - n * (2.0 * dot(n));
    }

    explicit operator CMatrix<3, 3>() const;

    template <int M>
    CVector<M> head() const {
        CNeeds<(M <= N)>::check();
        CVector<M> r;
        for (int i = 0; i < M; ++i) r[i] = scalar[i];
        return r;
    }

    template <int M>
    CVector<M> resized(double pad = 0.0) const {
        CNeeds<(M >= N)>::check();
        CVector<M> r;
        for (int i = 0; i < N; ++i) r[i] = scalar[i];
        for (int i = N; i < M; ++i) r[i] = pad;
        return r;
    }
};

template <int N>
CVector<N> operator+(CVector<N> a, const CVector<N>& b) { return a += b; }

template <int N>
CVector<N> operator-(CVector<N> a, const CVector<N>& b) { return a -= b; }

template <int N>
CVector<N> operator*(CVector<N> v, double s) { return v *= s; }

template <int N>
CVector<N> operator*(double s, CVector<N> v) { return v *= s; }

template <int N>
CVector<N> operator/(CVector<N> v, double s) { return v /= s; }

template <int N>
bool operator==(const CVector<N>& a, const CVector<N>& b) {
    for (int i = 0; i < N; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

template <int N>
bool operator!=(const CVector<N>& a, const CVector<N>& b) { return !(a == b); }

template <int N>
bool operator<(const CVector<N>& a, const CVector<N>& b) {
    for (int i = 0; i < N; ++i) {
        if (a[i] < b[i]) return true;
        if (b[i] < a[i]) return false;
    }
    return false;
}

template <int N, int M>
CVector<N + M> operator|(const CVector<N>& a, const CVector<M>& b) {
    CVector<N + M> r;
    for (int i = 0; i < N; ++i) r[i]     = a[i];
    for (int i = 0; i < M; ++i) r[N + i] = b[i];
    return r;
}

template <int N>
double dot(const CVector<N>& a, const CVector<N>& b) { return a.dot(b); }

template <int N>
double distance(const CVector<N>& a, const CVector<N>& b) { return (a - b).length(); }

template <int N>
double distanceSquared(const CVector<N>& a, const CVector<N>& b) {
    return (a - b).lengthSquared();
}

inline double cross(const CVector<2>& a, const CVector<2>& b) {
    return a[0] * b[1] - a[1] * b[0];
}

inline CVector<3> cross(const CVector<3>& a, const CVector<3>& b) {
    return CVector<3>(a[1] * b[2] - a[2] * b[1],
                      a[2] * b[0] - a[0] * b[2],
                      a[0] * b[1] - a[1] * b[0]);
}

template <int N>
double angleBetween(const CVector<N>& a, const CVector<N>& b) {
    double la = a.length(), lb = b.length();
    if (la == 0.0 || lb == 0.0) return 0.0;
    double c = a.dot(b) / (la * lb);
    if (c >  1.0) c =  1.0;
    if (c < -1.0) c = -1.0;
    return std::acos(c);
}

template <int N>
CVector<N> lerp(const CVector<N>& a, const CVector<N>& b, double t) {
    return a + (b - a) * t;
}

template <int N>
bool areOrthogonal(const CVector<N>& a, const CVector<N>& b, double tol = 1e-12) {
    return std::fabs(a.dot(b)) <= tol;
}

template <int N>
std::ostream& operator<<(std::ostream& os, const CVector<N>& v) {
    os << '(';
    for (int i = 0; i < N; ++i) {
        if (i > 0) os << ", ";
        os << v[i];
    }
    return os << ')';
}

template <int N>
std::istream& operator>>(std::istream& is, CVector<N>& v) {
    for (int i = 0; i < N; ++i) is >> v[i];
    return is;
}

typedef CVector<1> Vec1;
typedef CVector<2> Vec2;
typedef CVector<3> Vec3;
typedef CVector<4> Vec4;

#endif
