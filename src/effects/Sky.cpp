#include "Sky.h"

void SkyShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkyData", &Constants.SkyData);
	TheShaderManager->RegisterConstant("TESR_CloudData", &Constants.CloudData);
	TheShaderManager->RegisterConstant("TESR_CloudVolumeData", &Constants.CloudVolumeData);
	TheShaderManager->RegisterConstant("TESR_CloudVolumeData2", &Constants.CloudVolumeData2);
	TheShaderManager->RegisterConstant("TESR_CloudVolumeLight", &Constants.CloudVolumeLight);
	TheShaderManager->RegisterConstant("TESR_SunsetColor", &Constants.SunsetColor);
	TheShaderManager->RegisterConstant("TESR_SkyIrradiance", &Constants.Irradiance[0]);
	TheShaderManager->RegisterConstant("TESR_PhysSky", &Constants.PhysSky);
	TheShaderManager->RegisterConstant("TESR_PhysSkySun", &Constants.PhysSkySun);
}

namespace PhysAtmosphere {
	static const float Ground = 6360000.0f, Top = 6460000.0f;
	static const D3DXVECTOR3 Rayleigh(5.802e-6f, 13.558e-6f, 33.1e-6f);
	static const float RayleighH = 8000.0f, MieScatter = 3.996e-6f, MieExtinct = 4.44e-6f, MieH = 1200.0f, MieG = 0.8f;
	static const D3DXVECTOR3 Ozone(0.650e-6f, 1.881e-6f, 0.085e-6f);
	static const float OzoneColumn = 15000.0f, Pi = 3.14159265f;

	static float Chapman(float X, float h, float cosChi) {
		const float c = sqrtf(X + h);
		if (cosChi >= 0.0f) return c / (c * cosChi + 1.0f) * exp2f(-h);
		const float x0 = sqrtf(1.0f - cosChi * cosChi) * (X + h);
		return 2.0f * sqrtf(x0) * exp2f((std::min)(X - x0, 120.0f)) - c / (1.0f - c * cosChi) * exp2f(-h);
	}

	static D3DXVECTOR3 Exp(const D3DXVECTOR3& v) { return D3DXVECTOR3(expf(v.x), expf(v.y), expf(v.z)); }
	static D3DXVECTOR3 Mul(const D3DXVECTOR3& a, const D3DXVECTOR3& b) { return D3DXVECTOR3(a.x * b.x, a.y * b.y, a.z * b.z); }

	static D3DXVECTOR3 SunDepth(float r, float cosChi, float haze) {
		const float alt = r - Ground, hR = RayleighH * 0.693147f, hM = MieH * 0.693147f;
		const float chR = Chapman(Ground / hR, alt / hR, cosChi), chM = Chapman(Ground / hM, alt / hM, cosChi);
		const float airmass = chR / (std::max)(expf(-alt / RayleighH), 1e-6f);
		const float mie = MieExtinct * haze * MieH * chM;
		return Rayleigh * (RayleighH * chR) + D3DXVECTOR3(mie, mie, mie) + Ozone * (OzoneColumn * airmass);
	}

	static D3DXVECTOR3 Radiance(const D3DXVECTOR3& dir, const D3DXVECTOR3& sunDir, float camAlt, float haze) {
		const float r0 = Ground + camAlt, mu = dir.z;
		float tMax = -r0 * mu + sqrtf((std::max)(r0 * r0 * (mu * mu - 1.0f) + Top * Top, 0.0f));
		const float discGround = r0 * r0 * (mu * mu - 1.0f) + Ground * Ground;
		if (mu < 0.0f && discGround >= 0.0f) tMax = -r0 * mu - sqrtf(discGround);
		const float nu = D3DXVec3Dot(&dir, &sunDir);
		const float phaseR = 3.0f / (16.0f * Pi) * (1.0f + nu * nu);
		const float g2 = MieG * MieG;
		const float phaseM = 3.0f / (8.0f * Pi) * ((1.0f - g2) * (1.0f + nu * nu)) / ((2.0f + g2) * powf((std::max)(1.0f + g2 - 2.0f * MieG * nu, 1e-4f), 1.5f));
		D3DXVECTOR3 depth(0.0f, 0.0f, 0.0f), sumR(0.0f, 0.0f, 0.0f), sumM(0.0f, 0.0f, 0.0f);
		for (int i = 0; i < 12; i++) {
			const float s0 = i / 12.0f, s1 = (i + 1) / 12.0f, t0 = tMax * s0 * s0, t1 = tMax * s1 * s1, dt = t1 - t0;
			const D3DXVECTOR3 p = D3DXVECTOR3(0.0f, 0.0f, r0) + dir * (0.5f * (t0 + t1));
			const float r = D3DXVec3Length(&p), alt = r - Ground;
			const float dR = expf(-alt / RayleighH), dM = expf(-alt / MieH), dO = (std::max)(0.0f, 1.0f - fabsf(alt - 25000.0f) / 15000.0f);
			const float mie = MieExtinct * haze * dM;
			const D3DXVECTOR3 ext = Rayleigh * dR + D3DXVECTOR3(mie, mie, mie) + Ozone * dO;
			const D3DXVECTOR3 T = Exp(-(depth + ext * (0.5f * dt) + SunDepth(r, D3DXVec3Dot(&p, &sunDir) / r, haze)));
			sumR += T * (dR * dt);
			sumM += T * (dM * dt);
			depth += ext * dt;
		}
		return Mul(sumR, Rayleigh) * phaseR + sumM * (MieScatter * haze * phaseM);
	}

	static float Luma(const D3DXVECTOR3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

	static D3DXVECTOR3 Blend(const D3DXVECTOR3& game, const D3DXVECTOR3& phys, const D3DXVECTOR4& physSky) {
		const D3DXVECTOR3 tinted = phys + (game * (Luma(phys) / (std::max)(Luma(game), 1e-6f)) - phys) * physSky.z;
		return game + (tinted - game) * physSky.x;
	}
}

// Mirrors GetSunColor / GetSkyColor in Shaders/Includes/Sky.hlsl. Kept in step by hand: the
// two trees compile independently, exactly as Shadow.hlsl mirrors Effects/Includes/Shadows.hlsl.
// Change one and change the other.
//
// The atmosphere exponent is SKY.pso's own 8. The ambient path used to soften it to fake the
// width of a diffuse lobe; the integral below covers that properly, so the sky model is used
// unmodified and the ambient agrees with the dome on screen.
// The piecewise sRGB transfer function, matching linearize()/delinearize() in
// Shaders/Includes/Helpers.hlsl. A plain pow(c, 2.2) is NOT the same curve -- it diverges most
// in the darks, which is exactly where the sky colours sit at dawn and dusk.
static float SrgbToLinear(float c) {
	c = max(c, 0.0f);
	return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
}


static D3DXVECTOR3 Linearize(const D3DXVECTOR4& c) {
	return D3DXVECTOR3(SrgbToLinear(c.x), SrgbToLinear(c.y), SrgbToLinear(c.z));
}


static D3DXVECTOR3 EvalSky(const D3DXVECTOR3& dir, const D3DXVECTOR4& sunPos, const D3DXVECTOR4& skyData,
	const D3DXVECTOR3& skyC, const D3DXVECTOR3& skyLowC, const D3DXVECTOR3& horizonC,
	const D3DXVECTOR3& sunColor, float sunHeight, float isDayTime) {

	float verticality = powf(fabsf(dir.z * 0.5f + 0.5f), 3.0f);
	float athmosphere = powf(fabsf(1.0f - verticality), 8.0f) * skyData.x;
	float sunDot = (dir.x * sunPos.x + dir.y * sunPos.y + dir.z * sunPos.z) * 0.5f + 0.5f;
	float sunInfluence = powf(fabsf(sunDot), 1.0f / skyData.y);

	D3DXVECTOR3 c = skyLowC + (skyC - skyLowC) * verticality;
	float hz = std::clamp(athmosphere * (0.5f + sunInfluence * 0.5f), 0.0f, 1.0f);
	c = c + (horizonC - c) * hz;
	c += sunColor * (sunInfluence * (1.0f - sunHeight) * athmosphere * skyData.z * isDayTime);
	return c;
}

void SkyShaders::UpdateConstants() {
	if (TheShaderManager->Shaders.Tonemapping->Enabled)
		Constants.SunsetColor.w = TheShaderManager->GetTransitionValue(Settings.SkyMultiplierDay, Settings.SkyMultiplierNight, 1.0);
	else
		Constants.SunsetColor.w = 1.0;

	// Order-2 spherical harmonic projection of the sky, convolved with the clamped-cosine
	// kernel: E(N) = INTEGRAL L(w) max(N.w, 0) dw. The cosine kernel is a severe low-pass
	// filter, so 9 coefficients carry that integral to within about 1% (Ramamoorthi & Hanrahan
	// 2001), and the kernel has only three non-zero bands, A0 = pi, A1 = 2pi/3, A2 = pi/4.
	//
	// Integrated in LINEAR radiance, which is what the integral is defined on. The shader
	// encodes the reconstruction, because the object and terrain shaders carry no gamma
	// conversion and their ambient is gamma-encoded.
	//
	// Everything the ambient used to fake falls out of this: the wall/floor split, the sun-side
	// azimuthal bias, the cosine form factor, and the fact that a floor integrates the whole
	// dome while a wall integrates half. No directionality parameter is needed or meaningful.
	//
	// Radiance below the horizon is ZERO -- no ground bounce is modelled. That reproduces the
	// old form factor exactly: a floor sees the full dome, a wall half of it, a ceiling none.
	{
		PBRShaders* pbrForKey = TheShaderManager->Shaders.PBR;
		ProjectionInputs inputs = { TheShaderManager->ShaderConst.SunPosition, TheShaderManager->ShaderConst.skyColor,
			TheShaderManager->ShaderConst.skyLowColor, TheShaderManager->ShaderConst.horizonColor, Constants.SkyData,
			TheShaderManager->ShaderConst.sunDiskColor, Constants.SunsetColor,
			D3DXVECTOR4(PhysicalSettings.Enabled ? 1.0f : 0.0f, PhysicalSettings.Brightness, PhysicalSettings.WeatherTint, PhysicalSettings.Haze),
			TheShaderManager->ShaderConst.SunAmount.x,
			pbrForKey ? max(pbrForKey->SkylightSaturation, 0.0f) : 1.0f, TheShaderManager->GameState.isExterior ? 1.0f : 0.0f };
		inputs.sunsetColor.w = 0.0f;
		const bool sunStill = lastProjectionValid &&
			inputs.sunPos.x * lastProjection.sunPos.x + inputs.sunPos.y * lastProjection.sunPos.y + inputs.sunPos.z * lastProjection.sunPos.z >= 0.9999985f;
		if (sunStill) inputs.sunPos = lastProjection.sunPos;
		if (lastProjectionValid && memcmp(&inputs, &lastProjection, sizeof(inputs)) == 0) return;
		lastProjection = inputs;
		lastProjectionValid = true;
	}

	const D3DXVECTOR4& sunPos = TheShaderManager->ShaderConst.SunPosition;
	D3DXVECTOR3 skyC = Linearize(TheShaderManager->ShaderConst.skyColor);
	D3DXVECTOR3 skyLowC = Linearize(TheShaderManager->ShaderConst.skyLowColor);
	D3DXVECTOR3 horizonC = Linearize(TheShaderManager->ShaderConst.horizonColor);

	float sunHeight = max(sunPos.z, 0.0f);   // shade(sunPos, up)
	float dayTime = TheShaderManager->ShaderConst.SunAmount.x;
	float isDayTime = std::clamp(dayTime / 0.5f, 0.0f, 1.0f);
	isDayTime = isDayTime * isDayTime * (3.0f - 2.0f * isDayTime);   // smoothstep(0, 0.5, x)

	D3DXVECTOR3 sunDisk = Linearize(TheShaderManager->ShaderConst.sunDiskColor);
	D3DXVECTOR3 sunsetC = Linearize(Constants.SunsetColor);
	float sunSet = std::clamp(powf(1.0f - sunHeight, 8.0f), 0.0f, 1.0f) * dayTime;
	D3DXVECTOR3 sunColor = (1.0f + sunHeight) * sunDisk + sunsetC * (sunSet * Constants.SkyData.x);

	// Upper hemisphere only; the lower half contributes nothing, so it is skipped rather than
	// evaluated. Order-2 SH is smooth enough that this stratification is ample.
	const int STEPS_T = 16, STEPS_P = 32;
	const float dTheta = (D3DX_PI * 0.5f) / STEPS_T;
	const float dPhi = (2.0f * D3DX_PI) / STEPS_P;

	D3DXVECTOR3 sh[9];
	for (int i = 0; i < 9; i++) sh[i] = D3DXVECTOR3(0.0f, 0.0f, 0.0f);

	{
		D3DXVECTOR3 realSun(sunPos.x, sunPos.y, sunPos.z);
		if (!(TheShaderManager->GameState.isExterior && TheShaderManager->GameState.dayLight > 0.5f)) realSun.z = -realSun.z;
		float share = std::clamp((realSun.z + 0.05f) / 0.1f, 0.0f, 1.0f);
		share = share * share * (3.0f - 2.0f * share);
		if (!PhysicalSettings.Enabled || !TheShaderManager->GameState.isExterior) share = 0.0f;
		Constants.PhysSky = D3DXVECTOR4(share, 1.0f, PhysicalSettings.WeatherTint, PhysicalSettings.Haze);
		Constants.PhysSkySun = D3DXVECTOR4(realSun.x, realSun.y, realSun.z, 0.0f);
	}
	const bool physical = Constants.PhysSky.x > 0.0f;
	static D3DXVECTOR3 physSamples[(16 / 2) * (32 / 2)];
	if (physical) {
		const D3DXVECTOR3 realSun(Constants.PhysSkySun.x, Constants.PhysSkySun.y, Constants.PhysSkySun.z);
		for (int i = 0; i < STEPS_T / 2; i++) {
			const float theta = (2 * i + 1) * dTheta, st = sinf(theta), ct = cosf(theta);
			for (int j = 0; j < STEPS_P / 2; j++) {
				const float phi = (2 * j + 1) * dPhi;
				D3DXVECTOR3 physDir(st * cosf(phi), st * sinf(phi), (std::max)(ct, 0.002f));
				D3DXVec3Normalize(&physDir, &physDir);
				physSamples[i * (STEPS_P / 2) + j] = PhysAtmosphere::Radiance(physDir, realSun, 300.0f, PhysicalSettings.Haze);
			}
		}
		float gameSum = 0.0f, physSum = 0.0f;
		for (int i = 0; i < STEPS_T; i++) {
			const float theta = (i + 0.5f) * dTheta, st = sinf(theta), ct = cosf(theta), dOmega = st * dTheta * dPhi;
			for (int j = 0; j < STEPS_P; j++) {
				const float phi = (j + 0.5f) * dPhi;
				const D3DXVECTOR3 d(st * cosf(phi), st * sinf(phi), ct);
				physSum += PhysAtmosphere::Luma(physSamples[(i / 2) * (STEPS_P / 2) + j / 2]) * dOmega;
				gameSum += PhysAtmosphere::Luma(EvalSky(d, sunPos, Constants.SkyData, skyC, skyLowC, horizonC, sunColor, sunHeight, isDayTime)) * dOmega;
			}
		}
		Constants.PhysSky.y = physSum > 1e-12f ? PhysicalSettings.Brightness * gameSum / physSum : 0.0f;
	}

	for (int i = 0; i < STEPS_T; i++) {
		float theta = (i + 0.5f) * dTheta;
		float st = sinf(theta), ct = cosf(theta);
		float dOmega = st * dTheta * dPhi;
		for (int j = 0; j < STEPS_P; j++) {
			float phi = (j + 0.5f) * dPhi;
			D3DXVECTOR3 d(st * cosf(phi), st * sinf(phi), ct);

			// LINEAR radiance, as GetSkyColor returns and as the integral requires. The shader
			// encodes on reconstruction. Integrating encoded values instead was measurably
			// wrong -- it cost walls about a third of their brightness, since their lobe spans
			// the widest range of sky and a concave curve pulls an average of spread values
			// down hardest.
			D3DXVECTOR3 L = EvalSky(d, sunPos, Constants.SkyData, skyC, skyLowC, horizonC,
				sunColor, sunHeight, isDayTime);
			if (physical) L = PhysAtmosphere::Blend(L, physSamples[(i / 2) * (STEPS_P / 2) + j / 2] * Constants.PhysSky.y, Constants.PhysSky);
			L *= dOmega;

			sh[0] += L * 0.282095f;
			sh[1] += L * (0.488603f * d.y);
			sh[2] += L * (0.488603f * d.z);
			sh[3] += L * (0.488603f * d.x);
			sh[4] += L * (1.092548f * d.x * d.y);
			sh[5] += L * (1.092548f * d.y * d.z);
			sh[6] += L * (0.315392f * (3.0f * d.z * d.z - 1.0f));
			sh[7] += L * (1.092548f * d.x * d.z);
			sh[8] += L * (0.546274f * (d.x * d.x - d.y * d.y));
		}
	}

	// Convolve, and fold in each basis function's constant plus the 1/pi that converts
	// irradiance back to an average radiance -- the units the ambient path already works in, so
	// the existing scales keep their meaning. A0/pi = 1, A1/pi = 2/3, A2/pi = 1/4.
	const float A0 = 1.0f, A1 = 2.0f / 3.0f, A2 = 0.25f;
	const float band[9] = { A0, A1, A1, A1, A2, A2, A2, A2, A2 };
	const float basis[9] = { 0.282095f, 0.488603f, 0.488603f, 0.488603f,
							 1.092548f, 1.092548f, 0.315392f, 1.092548f, 0.546274f };

	PBRShaders* pbr = TheShaderManager->Shaders.PBR;
	const float saturation = pbr ? max(pbr->SkylightSaturation, 0.0f) : 1.0f;
	for (int i = 0; i < 9; i++) {
		D3DXVECTOR3 c = sh[i] * (band[i] * basis[i]);
		if (saturation != 1.0f) {
			const float luminance = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
			c = D3DXVECTOR3(luminance, luminance, luminance) + (c - D3DXVECTOR3(luminance, luminance, luminance)) * saturation;
		}
		Constants.Irradiance[i] = D3DXVECTOR4(c.x, c.y, c.z, 0.0f);
	}
}

void SkyShaders::UpdateSettings() {

	useSunDiskColor = TheSettingManager->GetSettingF("Shaders.Sky.Main", "UseSunDiskColor");

	Settings.SkyMultiplierDay = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "SkyMultiplier");
	Settings.SkyMultiplierNight = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "SkyMultiplier");

	Constants.SkyData.x = TheSettingManager->GetSettingF("Shaders.Sky.Main", "AthmosphereThickness");
	Constants.SkyData.y = TheSettingManager->GetSettingF("Shaders.Sky.Main", "SunInfluence");
	Constants.SkyData.z = TheSettingManager->GetSettingF("Shaders.Sky.Main", "SunStrength");
	Constants.SkyData.w = TheSettingManager->GetSettingF("Shaders.Sky.Main", "StarStrength");

	Constants.CloudData.x = TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "UseNormals");
	//Constants.CloudData.y = TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "SphericalNormals"); // not used much so it's disabled
	Constants.CloudData.z = TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Transparency");
	Constants.CloudData.w = TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Brightness");

	Constants.CloudData.y = TheSettingManager->GetSettingF("Shaders.Sky.Main", "StarTwinkle");

	PhysicalSettings.Enabled = TheSettingManager->GetSettingI("Shaders.Sky.Physical", "Enabled") != 0;
	PhysicalSettings.Brightness = std::clamp(TheSettingManager->GetSettingF("Shaders.Sky.Physical", "Brightness"), 0.0f, 4.0f);
	PhysicalSettings.WeatherTint = std::clamp(TheSettingManager->GetSettingF("Shaders.Sky.Physical", "WeatherTint"), 0.0f, 1.0f);
	PhysicalSettings.Haze = std::clamp(TheSettingManager->GetSettingF("Shaders.Sky.Physical", "Haze"), 0.0f, 10.0f);
	lastProjectionValid = false;

	Constants.CloudVolumeData.x = TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Volumetric");
	Constants.CloudVolumeData.y = std::clamp(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Steps"), 1.0f, 16.0f);
	Constants.CloudVolumeData.z = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "CloudHeight"), 0.0f);
	Constants.CloudVolumeData.w = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "CloudThickness"), 0.0f);
	Constants.CloudVolumeData2.x = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Density"), 0.0f);
	Constants.CloudVolumeData2.y = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Absorption"), 0.0f);
	Constants.CloudVolumeData2.z = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "DetailScale"), 0.0f) * 0.001f;
	Constants.CloudVolumeData2.w = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "UVScale"), 0.0f) * 0.001f;
	Constants.CloudVolumeLight.x = std::clamp(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "Scattering"), 0.0f, 0.9f);
	Constants.CloudVolumeLight.y = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "SilverLining"), 0.0f);
	Constants.CloudVolumeLight.z = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "AmbientStrength"), 0.0f);
	Constants.CloudVolumeLight.w = max(TheSettingManager->GetSettingF("Shaders.Sky.Clouds", "DetailSpeed"), 0.0f);

	// only add sunset color boost in exteriors
	if (TheShaderManager->GameState.isExterior) {
		Constants.SunsetColor.x = TheSettingManager->GetSettingF("Shaders.Sky.Main", "SunsetR");
		Constants.SunsetColor.y = TheSettingManager->GetSettingF("Shaders.Sky.Main", "SunsetG");
		Constants.SunsetColor.z = TheSettingManager->GetSettingF("Shaders.Sky.Main", "SunsetB");
	}
	else {
		Constants.SunsetColor.x = 0;
		Constants.SunsetColor.y = 0;
		Constants.SunsetColor.z = 0;
	}

}