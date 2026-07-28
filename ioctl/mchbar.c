// SPDX-License-Identifier: Unlicense

#include "cpuid.h"
#include "ioctl.h"
#include "mchbar.h"
#include <libnw.h>

static struct
{
	bool init_done;
	struct cpu_id_t* id;
	intel_microarch_t type;
	uint64_t mchbar_base;
	uint64_t pch_base;
} ctx;

intel_microarch_t mchbar_get_microarch(void)
{
	return ctx.type;
}

uint64_t
mchbar_get_mmio_reg(void)
{
	return ctx.mchbar_base;
}

uint64_t
pch_get_mmio_reg(void)
{
	return ctx.pch_base;
}

uint32_t
mchbar_read_32(uint64_t offset)
{
	if (!ctx.mchbar_base)
		return 0;

	uint32_t data = 0;
	int ret;
	if (NWLC->NwDrv->type == WR0_DRIVER_PAWNIO)
	{
		ULONG64 out = 0;
		ret = WR0_ExecPawn(NWLC->NwDrv, &NWLC->NwDrv->pio_mchbar, "ioctl_read_dword", &offset, 1, &out, 1, NULL);
		data = (uint32_t)out;
	}
	else
		ret = WR0_RdMmIo(NWLC->NwDrv, ctx.mchbar_base + offset, &data, sizeof(uint32_t));
	if (ret)
		NWL_Debug("MCHBAR", "MMIO 32 @%llX+%llx failed.", ctx.mchbar_base, offset);
	return data;
}

uint32_t
pch_read_32(uint64_t offset)
{
	if (!ctx.pch_base)
		return 0;

	uint32_t data = 0;
	int ret;
	if (NWLC->NwDrv->type == WR0_DRIVER_PAWNIO)
	{
		ULONG64 out[2] = { 0 };
		ret = WR0_ExecPawn(NWLC->NwDrv, &NWLC->NwDrv->pio_pch, "ioctl_read_raw", NULL, 0, out, 2, NULL);
		data = (uint32_t)out[0];
	}
	else
		ret = WR0_RdMmIo(NWLC->NwDrv, ctx.pch_base + offset, &data, sizeof(uint32_t));
	if (ret)
		NWL_Debug("PCH", "MMIO 32 @%llX+%llx failed.", ctx.pch_base, offset);
	return data;
}

uint64_t
mchbar_read_64(uint64_t offset)
{
	if (!ctx.mchbar_base)
		return 0;

	uint64_t data = 0;
	int ret;
	if (NWLC->NwDrv->type == WR0_DRIVER_PAWNIO)
	{
		ULONG64 out = 0;
		ret = WR0_ExecPawn(NWLC->NwDrv, &NWLC->NwDrv->pio_mchbar, "ioctl_read_qword", &offset, 1, &out, 1, NULL);
		data = out;
	}
	else
		ret = WR0_RdMmIo(NWLC->NwDrv, ctx.mchbar_base + offset, &data, sizeof(uint64_t));
	if (ret)
		NWL_Debug("MCHBAR", "MMIO 64 @%llX+%llx failed.", ctx.mchbar_base, offset);
	return data;
}

#define MCHBAR_BASE_REG_LOW     0x48
#define MCHBAR_BASE_REG_HIGH    0x4C

static uint64_t
mchbar_get_base_64(uint64_t base_mask)
{
	if (NWLC->NwDrv->type == WR0_DRIVER_PAWNIO)
	{
		ULONG64 out = 0;
		WR0_ExecPawn(NWLC->NwDrv, &NWLC->NwDrv->pio_mchbar, "ioctl_get_mchbar_addr", NULL, 0, &out, 1, NULL);
		return out;
	}

	uint64_t mmio_reg = 0;
	mmio_reg = WR0_RdPciConf32(NWLC->NwDrv, 0, MCHBAR_BASE_REG_LOW);
	if ((mmio_reg & 0x01) == 0)
		return 0; // MCHBAR is disabled
	if (mmio_reg == 0xFFFFFFFF)
		return 0;

	uint64_t mmio_reg_high = WR0_RdPciConf32(NWLC->NwDrv, 0, MCHBAR_BASE_REG_HIGH);
	if (mmio_reg_high == 0xFFFFFFFF)
		return 0;

	mmio_reg |= mmio_reg_high << 32;
	mmio_reg &= base_mask;
	return mmio_reg;
}

static inline void
identify_f05(struct cpu_id_t* id)
{
	ctx.type.cpu_type = INTEL_CPU_TYPE_PENTIUM;
}

static inline void
identify_f06(struct cpu_id_t* id)
{
	ctx.type.cpu_type = INTEL_CPU_TYPE_ATOM;

	switch (id->x86.ext_model)
	{
	case 0x01:
	case 0x03:
	case 0x05:
	case 0x0B:
	case 0x0D:
		ctx.type.cpu_type = INTEL_CPU_TYPE_PENTIUM;
		break;
	case 0x0E:
		ctx.type.microarch = INTEL_CORE_YONAH;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x0F:
		ctx.type.microarch = INTEL_CORE2_MEROM;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x16:
		ctx.type.microarch = INTEL_CORE2_MEROM_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x17:
		ctx.type.microarch = INTEL_CORE2_PENRYN;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x1D:
		ctx.type.microarch = INTEL_CORE2_DUNNINGTON;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x1E:
		ctx.type.microarch = INTEL_NEHALEM;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x1F:
		ctx.type.microarch = INTEL_NEHALEM_G;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x1A:
		ctx.type.microarch = INTEL_NEHALEM_EP;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x2E:
		ctx.type.microarch = INTEL_NEHALEM_EX;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x25:
		ctx.type.microarch = INTEL_WESTMERE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x2C:
		ctx.type.microarch = INTEL_WESTMERE_EP;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x2F:
		ctx.type.microarch = INTEL_WESTMERE_EX;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x2A:
		ctx.type.microarch = INTEL_SANDYBRIDGE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x2D:
		ctx.type.microarch = INTEL_SANDYBRIDGE_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x3A:
		ctx.type.microarch = INTEL_IVYBRIDGE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x3E:
		ctx.type.microarch = INTEL_IVYBRIDGE_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x3C:
		ctx.type.microarch = INTEL_HASWELL;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x3F:
		ctx.type.microarch = INTEL_HASWELL_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x45:
		ctx.type.microarch = INTEL_HASWELL_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x46:
		ctx.type.microarch = INTEL_HASWELL_G;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x3D:
		ctx.type.microarch = INTEL_BROADWELL;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x47:
		ctx.type.microarch = INTEL_BROADWELL_G;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x4F:
		ctx.type.microarch = INTEL_BROADWELL_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x56:
		ctx.type.microarch = INTEL_BROADWELL_D;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x4E:
		ctx.type.microarch = INTEL_SKYLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x5E:
		ctx.type.microarch = INTEL_SKYLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x55:
		ctx.type.microarch = INTEL_SKYLAKE_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x8E:
		ctx.type.microarch = INTEL_KABYLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x9E:
		ctx.type.microarch = INTEL_KABYLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xA5:
		ctx.type.microarch = INTEL_COMETLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xA6:
		ctx.type.microarch = INTEL_COMETLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x66:
		ctx.type.microarch = INTEL_CANNONLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x6A:
		ctx.type.microarch = INTEL_ICELAKE_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x6C:
		ctx.type.microarch = INTEL_ICELAKE_D;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x7D:
		ctx.type.microarch = INTEL_ICELAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x7E:
		ctx.type.microarch = INTEL_ICELAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x9D:
		ctx.type.microarch = INTEL_ICELAKE_NNPI;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xA7:
		ctx.type.microarch = INTEL_ROCKETLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x8C:
		ctx.type.microarch = INTEL_TIGERLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x8D:
		ctx.type.microarch = INTEL_TIGERLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x8F:
		ctx.type.microarch = INTEL_SAPPHIRERAPIDS_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xCF:
		ctx.type.microarch = INTEL_EMERALDRAPIDS_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xAD:
		ctx.type.microarch = INTEL_GRANITERAPIDS_X;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xAE:
		ctx.type.microarch = INTEL_GRANITERAPIDS_D;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xD7:
		ctx.type.microarch = INTEL_BARTLETTLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x8A:
		ctx.type.microarch = INTEL_LAKEFIELD;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x97:
		ctx.type.microarch = INTEL_ALDERLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0x9A:
		ctx.type.microarch = INTEL_ALDERLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xB7:
		ctx.type.microarch = INTEL_RAPTORLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xBA:
		ctx.type.microarch = INTEL_RAPTORLAKE_P;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xBF:
		ctx.type.microarch = INTEL_RAPTORLAKE_S;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xAC:
		ctx.type.microarch = INTEL_METEORLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xAA:
		ctx.type.microarch = INTEL_METEORLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xC5:
		ctx.type.microarch = INTEL_ARROWLAKE_H;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xC6:
		ctx.type.microarch = INTEL_ARROWLAKE;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xB5:
		ctx.type.microarch = INTEL_ARROWLAKE_U;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xBD:
		ctx.type.microarch = INTEL_LUNARLAKE_M;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xCC:
		ctx.type.microarch = INTEL_PANTHERLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xE5:
		ctx.type.microarch = INTEL_PANTHERLAKE_R;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	case 0xD5:
		ctx.type.microarch = INTEL_WILDCATLAKE_L;
		ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
		break;
	}
}

static inline void
identify_f15(struct cpu_id_t* id)
{
	ctx.type.cpu_type = INTEL_CPU_TYPE_PENTIUM;
}

static inline void
identify_f18(struct cpu_id_t* id)
{
	ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
	switch (id->x86.ext_model)
	{
	case 0x01:
		ctx.type.microarch = INTEL_NOVALAKE;
		break;
	case 0x03:
		ctx.type.microarch = INTEL_NOVALAKE_L;
		break;
	}
}

static inline void
identify_f19(struct cpu_id_t* id)
{
	ctx.type.cpu_type = INTEL_CPU_TYPE_CORE;
	switch (id->x86.ext_model)
	{
	case 0x01:
		ctx.type.microarch = INTEL_DIAMONDRAPIDS_X;
		break;
	}
}

static const uint16_t INTEL_LEGACY_THERMAL_IDS[] =
{
	0x06F9, // 400 Series
	0x9DF9, // 300 Series / C240 Series
	0xA379, // 300 Series / C240 Series
	0x02F9, // 300 Series CNL-LP PCH
	0xA2B1, // 200 Series / X299 / Z370 Series
	0xA131, // 100 Series / C230 Series
	0x9D31, // 100 Series Skylake PCH
	0x3A32, // 9 Series / 8 Series / C220 Series
	0x9CA4, // 9 Series Wildcat Point
	0x1C24, // 7 Series / C216 Series / 6 Series / C200 Series
	0x3B32, // 5 Series / 3400 Series
	0x9C24, // Haswell PCH
	0x8C24, // Haswell PCH
	0xA1B1, // C620 Series Lewisburg PCH
	0x8D24, // X99 Wellsburg
};

static const uint16_t INTEL_MODERN_LPC_IDS[] =
{
	0x4381,
	0x4385,
	0x4386,
	0x4387,
	0x4388,
	0x4389,
	0x438B,
	0x438F,
	0x7A83,
	0x7A84,
	0x7A85,
	0x7A86,
	0x7A87,
	0x7A88,
	0x7A04,
	0x7A05,
	0x7A06,
	0x7A0C,
	0xAE0C,
	0xAE0D,
	0x7F0C,
	0x7F0D,
	0x7F12,
	0x5182,
	0x519D,
	0x7E02,
	0xA807,
	0x7702,
	0x5481,
	0xE302,
	0xE402,
};

#define BAR_REG_LOW     0x10
#define BAR_REG_HIGH    0x14

#define PCI_BUS_PCH                     0x00
#define PCI_MAX_DEVICE                  0x20
#define PCI_MAX_FUNCTION                0x08

static bool detect_pch_thermal(void)
{
	if (NWLC->NwDrv->type == WR0_DRIVER_PAWNIO)
	{
		ULONG64 out[5] = { 0 };
		WR0_ExecPawn(NWLC->NwDrv, &NWLC->NwDrv->pio_pch, "ioctl_identity", NULL, 0, out, 5, NULL);
		switch (out[0])
		{
		case 1:
			ctx.pch_base = PWRMBASE_DEFAULT;
			NWL_Debug("PCH", "Found LPC Controller with ID %04X", (uint16_t)(out[1] >> 16));
			break;
		case 2:
			ctx.pch_base = out[3];
			NWL_Debug("PCH", "Found Thermal Sensor with ID %04X", (uint16_t)(out[1] >> 16));
			break;
		}
		if (ctx.pch_base)
			return true;
		return false;
	}

	for (uint32_t dev = 0; dev < PCI_MAX_DEVICE; dev++)
	{
		for (uint32_t func = 0; func < PCI_MAX_FUNCTION; func++)
		{
			uint32_t pci_addr = PciBusDevFunc(PCI_BUS_PCH, dev, func);
			uint32_t id = WR0_RdPciConf32(NWLC->NwDrv, pci_addr, 0);
			if (id == 0xFFFFFFFF)
				continue;
			if ((id & 0xFFFF) != 0x8086)
				continue;
			id = (id >> 16) & 0xFFFF;
			for (size_t i = 0; i < ARRAYSIZE(INTEL_MODERN_LPC_IDS); i++)
			{
				if (id == INTEL_MODERN_LPC_IDS[i])
				{
					NWL_Debug("PCH", "Found LPC Controller with ID %04X", id);
					ctx.pch_base = PWRMBASE_DEFAULT;
					return true;
				}
			}
			for (size_t i = 0; i < ARRAYSIZE(INTEL_LEGACY_THERMAL_IDS); i++)
			{
				if (id == INTEL_LEGACY_THERMAL_IDS[i])
				{
					NWL_Debug("PCH", "Found Thermal Sensor with ID %04X", id);
					uint64_t mmio_reg = 0;
					mmio_reg = WR0_RdPciConf32(NWLC->NwDrv, pci_addr, BAR_REG_LOW);
					if (mmio_reg == 0xFFFFFFFF)
						return false;
					mmio_reg &= 0xFFFFF000;
					uint64_t mmio_reg_high = WR0_RdPciConf32(NWLC->NwDrv, pci_addr, BAR_REG_HIGH);
					if (mmio_reg_high == 0xFFFFFFFF)
						return false;
					mmio_reg |= mmio_reg_high << 32;
					ctx.pch_base = mmio_reg;
					return true;
				}
			}
		}
	}
	return false;
}

bool
mchbar_pch_init(void)
{
	struct system_id_t* id = NWL_GetCpuid();

	if (ctx.init_done)
		return true;

	if (!NWLC->NwDrv)
		goto fail;

	if (!id)
		goto fail;

	ctx.id = &id->cpu_types[0];
	if (ctx.id->vendor != VENDOR_INTEL)
		goto fail;

	ctx.type.stepping = ctx.id->x86.stepping;
	ctx.type.model = ctx.id->x86.ext_model;
	ctx.type.family = ctx.id->x86.ext_family;

	switch (ctx.id->x86.ext_family)
	{
	case 0x05:
		identify_f05(ctx.id);
		break;
	case 0x06:
		identify_f06(ctx.id);
		break;
	case 0x15:
		identify_f15(ctx.id);
		break;
	case 0x18:
		identify_f18(ctx.id);
		break;
	case 0x19:
		identify_f19(ctx.id);
		break;
	}

	switch (ctx.type.cpu_type)
	{
	case INTEL_CPU_TYPE_UNKNOWN:
		goto fail;
	case INTEL_CPU_TYPE_CORE:
		ctx.mchbar_base = mchbar_get_base_64(0x3FFFFFF8000); // 41:15
	}

	detect_pch_thermal();

	NWL_Debug("MCHBAR", "MMIO REG %llX", ctx.mchbar_base);
	NWL_Debug("PCH", "MMIO REG %llX", ctx.pch_base);

	ctx.init_done = true;
	return true;

fail:
	ZeroMemory(&ctx, sizeof(ctx));
	return false;
}
