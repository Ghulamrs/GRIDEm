#ifndef CMATRIX_H
#define CMATRIX_H

#include "CVector.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iomanip>
#include <iostream>

template <int M = 3, int N = M>
class CMatrix {
public:
    CVector<N> vector[M];

    CMatrix() {}

    explicit CMatrix(double fill) {
        for (int i = 0; i < M; ++i) vector[i] = CVector<N>(fill);
    }

    CMatrix(std::initializer_list<CVector<N> > list) {
        assert(static_cast<int>(list.size()) == M &&
               "CMatrix: braced list has the wrong number of rows");
        int i = 0;
        for (typename std::initializer_list<CVector<N> >::const_iterator it = list.begin();
             it != list.end() && i < M; ++it, ++i)
            vector[i] = *it;
    }

    CMatrix(const CVector<N>& r0, const CVector<N>& r1) {
        CNeeds<(M == 2)>::check();
        vector[0] = r0;
        vector[1] = r1;
    }

    CMatrix(const CVector<N>& r0, const CVector<N>& r1, const CVector<N>& r2) {
        CNeeds<(M == 3)>::check();
        vector[0] = r0;
        vector[1] = r1;
        vector[2] = r2;
    }

    CMatrix(const CVector<N>& r0, const CVector<N>& r1,
            const CVector<N>& r2, const CVector<N>& r3) {
        CNeeds<(M == 4)>::check();
        vector[0] = r0;
        vector[1] = r1;
        vector[2] = r2;
        vector[3] = r3;
    }

    explicit CMatrix(const double (&a)[M][N]) {
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < N; ++j) vector[i][j] = a[i][j];
    }

    static CMatrix zero() { return CMatrix(); }

    static CMatrix identity() {
        CNeeds<(M == N)>::check();
        CMatrix r;
        for (int i = 0; i < M; ++i) r.vector[i][i] = 1.0;
        return r;
    }

    static CMatrix diagonal(const CVector<N>& d) {
        CNeeds<(M == N)>::check();
        CMatrix r;
        for (int i = 0; i < M; ++i) r.vector[i][i] = d[i];
        return r;
    }

    static CMatrix rotation(double radians) {
        CNeeds<(M == 2 && N == 2)>::check();
        double c = std::cos(radians), s = std::sin(radians);
        CMatrix r;
        r[0][0] = c; r[0][1] = -s;
        r[1][0] = s; r[1][1] =  c;
        return r;
    }

    static CMatrix rotationX(double radians) {
        CNeeds<(M == 3 && N == 3)>::check();
        double c = std::cos(radians), s = std::sin(radians);
        CMatrix r;
        r[0][0] = 1.0;
        r[1][1] = c; r[1][2] = -s;
        r[2][1] = s; r[2][2] =  c;
        return r;
    }

    static CMatrix rotationY(double radians) {
        CNeeds<(M == 3 && N == 3)>::check();
        double c = std::cos(radians), s = std::sin(radians);
        CMatrix r;
        r[0][0] =  c; r[0][2] = s;
        r[1][1] = 1.0;
        r[2][0] = -s; r[2][2] = c;
        return r;
    }

    static CMatrix rotationZ(double radians) {
        CNeeds<(M == 3 && N == 3)>::check();
        double c = std::cos(radians), s = std::sin(radians);
        CMatrix r;
        r[0][0] = c; r[0][1] = -s;
        r[1][0] = s; r[1][1] =  c;
        r[2][2] = 1.0;
        return r;
    }

    static CMatrix rotation(const CVector<3>& axis, double radians) {
        CNeeds<(M == 3 && N == 3)>::check();
        double len = axis.length();
        if (len == 0.0) return identity();
        double x = axis[0] / len, y = axis[1] / len, z = axis[2] / len;
        double c = std::cos(radians), s = std::sin(radians), t = 1.0 - c;
        CMatrix r;
        r[0][0] = c + x * x * t;     r[0][1] = x * y * t - z * s; r[0][2] = x * z * t + y * s;
        r[1][0] = y * x * t + z * s; r[1][1] = c + y * y * t;     r[1][2] = y * z * t - x * s;
        r[2][0] = z * x * t - y * s; r[2][1] = z * y * t + x * s; r[2][2] = c + z * z * t;
        return r;
    }

    CVector<N>&       operator[](int i)       { return vector[i]; }
    const CVector<N>& operator[](int i) const { return vector[i]; }

    int rows() const { return M; }
    int cols() const { return N; }

    CVector<M> col(int j) const {
        CVector<M> c;
        for (int i = 0; i < M; ++i) c[i] = vector[i][j];
        return c;
    }

    void setCol(int j, const CVector<M>& c) {
        for (int i = 0; i < M; ++i) vector[i][j] = c[i];
    }

    void swapRows(int a, int b) {
        CVector<N> t = vector[a];
        vector[a] = vector[b];
        vector[b] = t;
    }

    CMatrix& operator+=(const CMatrix& m) {
        for (int i = 0; i < M; ++i) vector[i] += m.vector[i];
        return *this;
    }

    CMatrix& operator-=(const CMatrix& m) {
        for (int i = 0; i < M; ++i) vector[i] -= m.vector[i];
        return *this;
    }

    CMatrix& operator*=(double s) {
        for (int i = 0; i < M; ++i) vector[i] *= s;
        return *this;
    }

    CMatrix& operator/=(double s) {
        for (int i = 0; i < M; ++i) vector[i] /= s;
        return *this;
    }

    CMatrix operator-() const {
        CMatrix r;
        for (int i = 0; i < M; ++i) r.vector[i] = -vector[i];
        return r;
    }

    CMatrix<N, M> transposed() const {
        CMatrix<N, M> t;
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < N; ++j) t[j][i] = vector[i][j];
        return t;
    }

    bool isSymmetric(double tol = 1e-12) const {
        if (M != N) return false;
        for (int i = 0; i < M; ++i)
            for (int j = i + 1; j < N; ++j)
                if (std::fabs(vector[i][j] - vector[j][i]) > tol) return false;
        return true;
    }

    bool isRotation(double tol = 1e-9) const {
        CNeeds<(M == N)>::check();
        for (int i = 0; i < M; ++i) {
            if (std::fabs(vector[i].lengthSquared() - 1.0) > tol) return false;
            for (int j = i + 1; j < M; ++j)
                if (std::fabs(vector[i].dot(vector[j])) > tol) return false;
        }
        return std::fabs(det() - 1.0) <= tol;
    }

    double trace() const {
        CNeeds<(M == N)>::check();
        double s = 0.0;
        for (int i = 0; i < M; ++i) s += vector[i][i];
        return s;
    }

    double det() const {
        CNeeds<(M == N)>::check();
        CMatrix a = *this;
        double d = 1.0;
        for (int i = 0; i < N; ++i) {
            int piv = i;
            for (int r = i + 1; r < N; ++r)
                if (std::fabs(a[r][i]) > std::fabs(a[piv][i])) piv = r;
            if (a[piv][i] == 0.0) return 0.0;
            if (piv != i) {
                a.swapRows(i, piv);
                d = -d;
            }
            d *= a[i][i];
            for (int r = i + 1; r < N; ++r)
                a[r] -= a[i] * (a[r][i] / a[i][i]);
        }
        return d;
    }

    bool invert(CMatrix& result) const {
        CNeeds<(M == N)>::check();
        CMatrix a = *this;
        CMatrix inv = identity();
        for (int i = 0; i < N; ++i) {
            int piv = i;
            for (int r = i + 1; r < N; ++r)
                if (std::fabs(a[r][i]) > std::fabs(a[piv][i])) piv = r;
            if (std::fabs(a[piv][i]) < 1e-300) return false;
            if (piv != i) {
                a.swapRows(i, piv);
                inv.swapRows(i, piv);
            }
            double p = a[i][i];
            a[i] /= p;
            inv[i] /= p;
            for (int r = 0; r < N; ++r) {
                if (r == i) continue;
                double f = a[r][i];
                a[r]   -= a[i] * f;
                inv[r] -= inv[i] * f;
            }
        }
        result = inv;
        return true;
    }

    CMatrix inverse() const {
        CMatrix r;
        if (!invert(r)) return CMatrix();
        return r;
    }

    bool isInvertible() const {
        CMatrix ignored;
        return invert(ignored);
    }

    bool solve(const CVector<N>& b, CVector<N>& x) const {
        CNeeds<(M == N)>::check();
        CMatrix inv;
        if (!invert(inv)) return false;
        x = inv * b;
        return true;
    }

    void print(std::ostream& os = std::cout, int width = 10, int digits = 4) const {
        std::streamsize oldPrecision = os.precision();
        std::ios_base::fmtflags oldFlags = os.flags();
        os << std::fixed << std::setprecision(digits);
        for (int i = 0; i < M; ++i) {
            os << "| ";
            for (int j = 0; j < N; ++j) os << std::setw(width) << vector[i][j] << ' ';
            os << "|\n";
        }
        os.flags(oldFlags);
        os.precision(oldPrecision);
    }
};

template <int N>
CVector<N>::operator CMatrix<3, 3>() const {
    CNeeds<(N == 3)>::check();
    CMatrix<3, 3> s;
    s[0][1] = -scalar[2];   s[0][2] =  scalar[1];
    s[1][0] =  scalar[2];   s[1][2] = -scalar[0];
    s[2][0] = -scalar[1];   s[2][1] =  scalar[0];
    return s;
}

template <int M, int N>
CMatrix<M, N> operator+(CMatrix<M, N> a, const CMatrix<M, N>& b) { return a += b; }

template <int M, int N>
CMatrix<M, N> operator-(CMatrix<M, N> a, const CMatrix<M, N>& b) { return a -= b; }

template <int M, int N>
CMatrix<M, N> operator*(CMatrix<M, N> a, double s) { return a *= s; }

template <int M, int N>
CMatrix<M, N> operator*(double s, CMatrix<M, N> a) { return a *= s; }

template <int M, int N>
CMatrix<M, N> operator/(CMatrix<M, N> a, double s) { return a /= s; }

template <int M, int N>
CVector<M> operator*(const CMatrix<M, N>& a, const CVector<N>& x) {
    CVector<M> y;
    for (int i = 0; i < M; ++i) y[i] = a[i].dot(x);
    return y;
}

template <int M, int N>
CVector<N> operator*(const CVector<M>& x, const CMatrix<M, N>& a) {
    CVector<N> y;
    for (int i = 0; i < M; ++i) y += a[i] * x[i];
    return y;
}

template <int M, int N, int K>
CMatrix<M, K> operator*(const CMatrix<M, N>& a, const CMatrix<N, K>& b) {
    CMatrix<M, K> r;
    for (int i = 0; i < M; ++i)
        for (int k = 0; k < N; ++k)
            r[i] += b[k] * a[i][k];
    return r;
}

template <int M, int N>
bool operator==(const CMatrix<M, N>& a, const CMatrix<M, N>& b) {
    for (int i = 0; i < M; ++i)
        if (!(a[i] == b[i])) return false;
    return true;
}

template <int M, int N>
bool operator!=(const CMatrix<M, N>& a, const CMatrix<M, N>& b) { return !(a == b); }

template <int M, int N, int K>
CMatrix<M, N + K> operator|(const CMatrix<M, N>& a, const CMatrix<M, K>& b) {
    CMatrix<M, N + K> r;
    for (int i = 0; i < M; ++i) r[i] = a[i] | b[i];
    return r;
}

template <int M, int N>
CMatrix<M, N + 1> operator|(const CMatrix<M, N>& a, const CVector<M>& b) {
    CMatrix<M, N + 1> r;
    for (int i = 0; i < M; ++i) r[i] = a[i] | CVector<1>(b[i]);
    return r;
}

template <int M1, int M2, int N>
CMatrix<M1 + M2, N> stack(const CMatrix<M1, N>& a, const CMatrix<M2, N>& b) {
    CMatrix<M1 + M2, N> r;
    for (int i = 0; i < M1; ++i) r[i] = a[i];
    for (int i = 0; i < M2; ++i) r[M1 + i] = b[i];
    return r;
}

template <int M, int N>
CMatrix<N, M> transpose(const CMatrix<M, N>& a) { return a.transposed(); }

template <int N>
double det(const CMatrix<N, N>& a) { return a.det(); }

template <int N>
double trace(const CMatrix<N, N>& a) { return a.trace(); }

template <int M, int N>
CMatrix<M, N> outer(const CVector<M>& a, const CVector<N>& b) {
    CMatrix<M, N> r;
    for (int i = 0; i < M; ++i) r[i] = b * a[i];
    return r;
}

template <int M, int N>
std::ostream& operator<<(std::ostream& os, const CMatrix<M, N>& a) {
    os << '[';
    for (int i = 0; i < M; ++i) {
        if (i > 0) os << ", ";
        os << a[i];
    }
    return os << ']';
}

template <int M, int N>
std::istream& operator>>(std::istream& is, CMatrix<M, N>& a) {
    for (int i = 0; i < M; ++i) is >> a[i];
    return is;
}

typedef CMatrix<2> Mat2;
typedef CMatrix<3> Mat3;
typedef CMatrix<4> Mat4;

#endif
