#pragma once
#include "uTypedef.h"

/**
 * @brief running stats for numerical values
 */
class TrunningStats
{

private:
    // running stats variables
    dtypes::uint32 Fcount = 0;     // n: number of data points seen so far
    dtypes::float64 FrunningM = 0; // M_k: running mean
    dtypes::float64 FrunningT = 0; // T_k: sum of weighted squared deviations
    dtypes::float64 FrunningW = 0; // W_k: accumulated weight

public:
    // constructor
    TrunningStats()
    {
    }

    /**
     * @brief add a value to the running average
     * numerically stable running stats for value averaging are based on the
     * West update of Welford's algorithm for weighted averages and variance.
     * for a detailed discussion and equations see the following publications
     * - West 1979: dl.acm.org/doi/10.1145/359146.359153
     * - Schubert & Gertz 2018: dl.acm.org/doi/10.1145/3221269.3223036
     * Sum of weights W:
     * W_k = W_{k-1} + w_k
     * Weighted mean M:
     * M_k = M_{k-1} + w_k / W_k * (x_k - M_{k-1})
     * Weighted squared deviation T:
     * T_k = T_{k-1} + w_k / W_k * (x_k - M_{k-1}) * (x_k - M_{k-1}) * W_{k-1}
     * Variance S2:
     * S2 = T_n * n/((n-1) * W_n)
     * Optimized computation (West 1979):
     * Q = x_k - M_{k-1}
     * TEMP = W_{k-1} + w_k
     * R = Q * w_k / TEMP
     * M = M + R
     * T = T + R * W_{k-1} * Q
     * W_k = TEMP
     * @param _x new value x_k
     * @param _w weight w_k of the new value x_k
     */
    void add(dtypes::float64 _x, dtypes::float64 _w = 1.0)
    {
        Fcount++;
        dtypes::float64 Q = _x - FrunningM;
        dtypes::float64 TEMP = FrunningW + _w;
        dtypes::float64 R = Q * _w / TEMP;
        FrunningM += R;
        FrunningT += R * FrunningW * Q;
        FrunningW = TEMP;
    }

    dtypes::uint32 count()
    {
        return Fcount;
    }

    dtypes::float64 mean()
    {
        return FrunningM;
    }

    dtypes::float64 variance()
    {
        // sample variance (for population variance skip the -1)
        // Note: should this implement unbaised variance for small populations if count() is small?
        return ((Fcount > 1 && FrunningW > 0) ? FrunningT * Fcount / ((Fcount - 1) * FrunningW) : std::numeric_limits<dtypes::float64>::quiet_NaN());
    }

    dtypes::float64 stdDev()
    {
        return sqrt(variance());
    }

    void reset()
    {
        Fcount = 0;
        FrunningM = 0;
        FrunningT = 0;
        FrunningW = 0;
    }
};