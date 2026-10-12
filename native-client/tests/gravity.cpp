#include <Game/Gravity/PointGravity.hpp>
#include <Game/Gravity/DiskGravity.hpp>
#include <Game/Gravity/ParallelGravity.hpp>
#include <Game/Util/MathUtil.hpp>
#include <JSystem/JMath/JMath.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>

static void near(f32 actual, f32 expected, f32 tolerance = 0.001f) {
    assert(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance);
}

int main() {
    static_assert(sizeof(u32) == 4 && sizeof(s32) == 4 && sizeof(Vec) == 12);
    TVec3f a(1.0f, 2.0f, 3.0f), b(4.0f, -5.0f, 6.0f);
    TVec3f copy = a;
    copy = b;
    near(copy.y, -5.0f);
    near(a.dot(b), 12.0f);
    near(a.length(), std::sqrt(14.0f));
    near(DEG_TO_RAD(90.0f), HALF_PI);
    near(RAD_TO_DEG(HALF_PI), 90.0f);
    const TVec3f angles = DEG_TO_RAD(TVec3f(0.0f, 90.0f, 180.0f));
    near(angles.y, HALF_PI);
    near(angles.z, PI);
    TVec3f crossed = a.cross(b);
    near(crossed.x, 27.0f);
    near(crossed.y, 6.0f);
    near(crossed.z, -13.0f);
    JMAVECScaleAdd(&a, &b, &a, 2.0f);
    near(a.x, 6.0f);
    near(a.y, -1.0f);
    near(a.z, 12.0f);

    near(JMAHermiteInterpolation(2, 2, 10, 3, 6, 22, 3), 10);
    near(JMAHermiteInterpolation(4, 2, 10, 3, 6, 22, 3), 16);
    near(JMAHermiteInterpolation(6, 2, 10, 3, 6, 22, 3), 22);
    near(JMAHermiteInterpolation(4, 2, 10, 2, 6, 22, -1), 17.5f);
    near(JMASqrt(9.0f), 3.0f);
    Quaternion q{0, 0, 0, 1}, opposite{0, 0, 0, -1}, interpolated;
    JMAQuatLerp(&q, &opposite, 0.5f, &interpolated);
    near(interpolated.w, 1.0f);

    PointGravity gravity;
    TPos3f transform;
    transform.identity();
    transform.setTrans(10.0f, 0.0f, 0.0f);
    gravity.setOrig(TVec3f(0.0f, 0.0f, 0.0f));
    gravity.updateMtx(transform);
    TVec3f result;
    f32 distance;
    assert(gravity.calcOwnGravityVector(&result, &distance, TVec3f(13.0f, 4.0f, 0.0f)));
    near(distance, 5.0f);
    near(result.x, -0.6f);
    near(result.y, -0.8f);
    assert(gravity.calcGravity(&result, TVec3f(2010.0f, 0.0f, 0.0f)));
    near(result.x, -1.0f);
    gravity.mIsInverse = true;
    assert(gravity.calcGravity(&result, TVec3f(2010.0f, 0.0f, 0.0f)));
    near(result.x, 1.0f);
    assert(gravity.calcOwnGravityVector(&result, &distance, gravity.mTranslation));
    near(result.x, 0.0f);
    near(distance, 0.0f);
    gravity.mRange = 5.0f;
    result.set(7.0f, 8.0f, 9.0f);
    distance = 42.0f;
    assert(!gravity.calcOwnGravityVector(&result, &distance, TVec3f(20.0f, 0.0f, 0.0f)));
    near(result.x, 7.0f);
    near(distance, 42.0f);
    assert(!gravity.calcOwnGravityVector(nullptr, nullptr, TVec3f(15.0f, 0.0f, 0.0f)));
    assert(gravity.calcOwnGravityVector(nullptr, nullptr, TVec3f(14.0f, 0.0f, 0.0f)));
    TPos3f copied(&transform);
    TVec3f translation;
    copied.getTrans(translation);
    near(translation.x, 10.0f);

    ParallelGravity parallel;
    parallel.updateIdentityMtx();
    assert(parallel.calcOwnGravityVector(&result, &distance, TVec3f(0.0f, 10.0f, 0.0f)));
    near(result.y, -1.0f);
    DiskGravity disk;
    disk.updateIdentityMtx();
    assert(disk.calcOwnGravityVector(&result, &distance, TVec3f(0.0f, 100.0f, 0.0f)));
    near(result.y, -1.0f);
    near(distance, 50.0f);
    disk.setBothSide(false);
    assert(!disk.calcOwnGravityVector(&result, &distance, TVec3f(0.0f, 0.0f, 0.0f)));
    std::puts("Native Petari math, point, parallel, and disk gravity checks passed.");
}
