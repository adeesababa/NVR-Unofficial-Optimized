#pragma once

template <class DeviceType>
void UploadShadowBones(DeviceType* Device, const float* Matrices,
                       const unsigned short* BoneMap, unsigned int BoneCount) {

	if (!BoneCount) return;
	if (!BoneMap) {
		Device->SetVertexShaderConstantF(9, Matrices, BoneCount * 3);
		return;
	}
	for (unsigned int First = 0; First < BoneCount;) {
		unsigned int End = First + 1;
		while (End < BoneCount &&
		       static_cast<unsigned int>(BoneMap[End]) ==
		           static_cast<unsigned int>(BoneMap[End - 1]) + 1) {
			++End;
		}
		Device->SetVertexShaderConstantF(9 + First * 3,
		    Matrices + BoneMap[First] * 12, (End - First) * 3);
		First = End;
	}
}
