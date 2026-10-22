#ifndef LAMPFALLOFF_INCLUDED
#define LAMPFALLOFF_INCLUDED

#ifndef LAMP_FALLOFF_ON
    #define LAMP_FALLOFF_ON 0
#endif

#if LAMP_FALLOFF_ON
float4 TESR_LampFalloff : register(c205);
#endif

#ifdef LINEAR_LIGHTING
    #define LAMP_OLD_EXPONENT (TESR_LinearLighting.x > 1.0f ? (TESR_LinearLighting.z > 0.0f ? TESR_LinearLighting.z : TESR_LinearLighting.x) : 1.0f)
#else
    #define LAMP_OLD_EXPONENT 1.0f
#endif

#if LAMP_FALLOFF_ON >= 2
#ifdef LINEAR_LIGHTING
    #define LAMP_SHAPE_EXPONENT (max(TESR_LinearLighting.x, 1.0f) * (1.0f / 2.2f))
#else
    #define LAMP_SHAPE_EXPONENT (1.0f / 2.2f)
#endif

float LampShape(float att) {
    const float q = 1.0f - att;
    float window = 1.0f - q * q;
    window *= window;
    return TESR_LampFalloff.z * window / (1.0f + q * TESR_LampFalloff.y);
}

float LampFalloffPow(float att, float oldExponent) {
    const bool shaped = TESR_LampFalloff.x > 0.0f;
    return pow(abs(shaped ? LampShape(att) : att), shaped ? LAMP_SHAPE_EXPONENT : oldExponent);
}
#endif

float LampAtt(float att) {
#if LAMP_FALLOFF_ON >= 2
    return LampFalloffPow(att, LAMP_OLD_EXPONENT);
#else
    return LinFalloff(att);
#endif
}

float LampAttOther(float att) {
#if LAMP_FALLOFF_ON >= 2
    return LampFalloffPow(att, TESR_LampFalloff.w > 0.0f ? LAMP_OLD_EXPONENT : 1.0f);
#elif LAMP_FALLOFF_ON
    return TESR_LampFalloff.w > 0.0f ? LinFalloff(att) : att;
#else
    return att;
#endif
}

#endif
