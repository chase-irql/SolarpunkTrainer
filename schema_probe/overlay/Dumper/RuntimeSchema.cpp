// SPDX-License-Identifier: MIT

#include "RuntimeSchema.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Json/json.hpp"
#include "OffsetFinder/Offsets.h"
#include "Platform.h"
#include "Unreal/NameArray.h"
#include "Unreal/ObjectArray.h"
#include "Unreal/UnrealObjects.h"

namespace
{
	using json = nlohmann::json;

	struct BuildIdentity
	{
		uint32_t TimeDateStamp = 0;
		uint32_t SizeOfImage = 0;
		uint64_t FileSize = 0;
		std::string TextSha256;
		std::string Fingerprint;
	};

	std::string Hex(
		const uint8_t* bytes,
		size_t count)
	{
		static constexpr char Digits[] =
			"0123456789abcdef";
		std::string output(count * 2, '0');
		for (size_t index = 0; index < count; ++index)
		{
			output[index * 2] =
				Digits[(bytes[index] >> 4) & 0x0F];
			output[index * 2 + 1] =
				Digits[bytes[index] & 0x0F];
		}
		return output;
	}

	bool HashSha256(
		const uint8_t* data,
		size_t size,
		std::string& output)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		DWORD objectLength = 0;
		DWORD resultLength = 0;
		std::vector<uint8_t> hashObject;
		std::array<uint8_t, 32> digest{};

		if (BCryptOpenAlgorithmProvider(
			&algorithm,
			BCRYPT_SHA256_ALGORITHM,
			nullptr,
			0) < 0)
		{
			return false;
		}
		const auto closeAlgorithm = [&]()
		{
			if (hash)
				BCryptDestroyHash(hash);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
		};

		if (BCryptGetProperty(
			algorithm,
			BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&objectLength),
			sizeof(objectLength),
			&resultLength,
			0) < 0)
		{
			closeAlgorithm();
			return false;
		}

		hashObject.resize(objectLength);
		if (BCryptCreateHash(
			algorithm,
			&hash,
			hashObject.data(),
			static_cast<ULONG>(hashObject.size()),
			nullptr,
			0,
			0) < 0
			|| BCryptHashData(
				hash,
				const_cast<PUCHAR>(data),
				static_cast<ULONG>(size),
				0) < 0
			|| BCryptFinishHash(
				hash,
				digest.data(),
				static_cast<ULONG>(digest.size()),
				0) < 0)
		{
			closeAlgorithm();
			return false;
		}

		output = Hex(digest.data(), digest.size());
		closeAlgorithm();
		return true;
	}

	bool ReadBuildIdentity(BuildIdentity& identity)
	{
		std::array<wchar_t, 32768> modulePath{};
		const DWORD length = GetModuleFileNameW(
			nullptr,
			modulePath.data(),
			static_cast<DWORD>(modulePath.size()));
		if (!length || length >= modulePath.size())
			return false;

		const std::filesystem::path path(
			std::wstring_view(modulePath.data(), length));
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return false;

		stream.seekg(0, std::ios::end);
		const std::streamoff fileSize = stream.tellg();
		if (fileSize <= 0)
			return false;
		identity.FileSize = static_cast<uint64_t>(fileSize);
		stream.seekg(0, std::ios::beg);

		IMAGE_DOS_HEADER dos{};
		stream.read(
			reinterpret_cast<char*>(&dos),
			sizeof(dos));
		if (!stream
			|| dos.e_magic != IMAGE_DOS_SIGNATURE
			|| dos.e_lfanew <= 0)
		{
			return false;
		}

		stream.seekg(dos.e_lfanew, std::ios::beg);
		IMAGE_NT_HEADERS64 nt{};
		stream.read(
			reinterpret_cast<char*>(&nt),
			sizeof(nt));
		if (!stream
			|| nt.Signature != IMAGE_NT_SIGNATURE
			|| nt.OptionalHeader.Magic
				!= IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		{
			return false;
		}
		identity.TimeDateStamp =
			nt.FileHeader.TimeDateStamp;
		identity.SizeOfImage =
			nt.OptionalHeader.SizeOfImage;

		stream.seekg(
			dos.e_lfanew
				+ sizeof(uint32_t)
				+ sizeof(IMAGE_FILE_HEADER)
				+ nt.FileHeader.SizeOfOptionalHeader,
			std::ios::beg);

		IMAGE_SECTION_HEADER text{};
		bool foundText = false;
		for (uint16_t index = 0;
			index < nt.FileHeader.NumberOfSections;
			++index)
		{
			IMAGE_SECTION_HEADER section{};
			stream.read(
				reinterpret_cast<char*>(&section),
				sizeof(section));
			if (!stream)
				return false;
			const std::string_view name(
				reinterpret_cast<const char*>(
					section.Name),
				strnlen_s(
					reinterpret_cast<const char*>(
						section.Name),
					IMAGE_SIZEOF_SHORT_NAME));
			if (name == ".text")
			{
				text = section;
				foundText = true;
				break;
			}
		}
		if (!foundText
			|| !text.PointerToRawData
			|| !text.SizeOfRawData)
		{
			return false;
		}

		std::vector<uint8_t> bytes(text.SizeOfRawData);
		stream.seekg(
			text.PointerToRawData,
			std::ios::beg);
		stream.read(
			reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!stream
			|| !HashSha256(
				bytes.data(),
				bytes.size(),
				identity.TextSha256))
		{
			return false;
		}

		std::ostringstream fingerprint;
		fingerprint
			<< std::hex
			<< std::setfill('0')
			<< std::setw(8)
			<< identity.TimeDateStamp
			<< '-'
			<< std::setw(8)
			<< identity.SizeOfImage
			<< '-'
			<< identity.TextSha256;
		identity.Fingerprint = fingerprint.str();
		return true;
	}

	std::filesystem::path GetSchemaPath(
		const BuildIdentity& identity)
	{
		std::array<wchar_t, 32768> localAppData{};
		const DWORD length = GetEnvironmentVariableW(
			L"LOCALAPPDATA",
			localAppData.data(),
			static_cast<DWORD>(localAppData.size()));
		if (!length || length >= localAppData.size())
			return {};

		return std::filesystem::path(
			std::wstring_view(
				localAppData.data(),
				length))
			/ L"Solarpunk Trainer"
			/ L"SchemaCache"
			/ std::filesystem::path(
				identity.Fingerprint)
			/ L"runtime-schema.json";
	}

	json DescribeProperty(const UEProperty& property)
	{
		json output{
			{ "name", property.GetName() },
			{ "class", property.GetPropClassName() },
			{ "cpp_type", property.GetCppType() },
			{ "offset", property.GetOffset() },
			{ "size", property.GetSize() },
			{ "array_dim", property.GetArrayDim() },
			{ "flags", static_cast<uint64_t>(
				property.GetPropertyFlags()) }
		};

		if (property.IsA(EClassCastFlags::BoolProperty))
		{
			const UEBoolProperty boolProperty =
				property.Cast<UEBoolProperty>();
			output["bool"] = {
				{ "byte_offset",
					boolProperty.GetByteOffset() },
				{ "field_mask",
					boolProperty.GetFieldMask() },
				{ "bit_index",
					boolProperty.GetBitIndex() },
				{ "native",
					boolProperty.IsNativeBool() }
			};
		}
		else if (property.IsA(
			EClassCastFlags::StructProperty))
		{
			const UEStruct referenced =
				property.Cast<UEStructProperty>()
					.GetUnderlayingStruct();
			if (referenced)
				output["referenced_type"] =
					referenced.GetPathName();
		}
		else if (property.IsA(
			EClassCastFlags::ObjectProperty
				| EClassCastFlags::ClassProperty
				| EClassCastFlags::WeakObjectProperty
				| EClassCastFlags::LazyObjectProperty
				| EClassCastFlags::SoftObjectProperty
				| EClassCastFlags::SoftClassProperty
				| EClassCastFlags::InterfaceProperty))
		{
			const UEClass referenced =
				property.Cast<UEObjectProperty>()
					.GetPropertyClass();
			if (referenced)
				output["referenced_type"] =
					referenced.GetPathName();
		}
		else if (property.IsA(
			EClassCastFlags::ArrayProperty))
		{
			const UEProperty inner =
				property.Cast<UEArrayProperty>()
					.GetInnerProperty();
			if (inner)
				output["inner"] =
					DescribeProperty(inner);
		}
		else if (property.IsA(
			EClassCastFlags::MapProperty))
		{
			const UEMapProperty map =
				property.Cast<UEMapProperty>();
			const UEProperty key = map.GetKeyProperty();
			const UEProperty value = map.GetValueProperty();
			if (key)
				output["key"] = DescribeProperty(key);
			if (value)
				output["value"] =
					DescribeProperty(value);
		}
		else if (property.IsA(
			EClassCastFlags::SetProperty))
		{
			const UEProperty element =
				property.Cast<UESetProperty>()
					.GetElementProperty();
			if (element)
				output["element"] =
					DescribeProperty(element);
		}
		return output;
	}

	json CoreSchema()
	{
		return {
			{ "globals", {
				{ "gobjects_rva",
					Off::InSDK::ObjArray::GObjects },
				{ "gnames_rva",
					Off::InSDK::NameArray::GNames },
				{ "gworld_rva",
					Off::InSDK::World::GWorld },
				{ "process_event_rva",
					Off::InSDK::ProcessEvent::PEOffset },
				{ "process_event_index",
					Off::InSDK::ProcessEvent::PEIndex }
			} },
			{ "object_array", {
				{ "chunked",
					Off::FUObjectArray::bIsChunked },
				{ "objects",
					Off::FUObjectArray::GetObjectsOffset() },
				{ "num_elements",
					Off::FUObjectArray::GetNumElementsOffset() },
				{ "max_elements",
					Off::FUObjectArray::GetMaxElementsOffset() },
				{ "num_chunks",
					Off::FUObjectArray::GetNumChunksOffset() },
				{ "max_chunks",
					Off::FUObjectArray::GetMaxChunksOffset() },
				{ "item_size",
					Off::InSDK::ObjArray::FUObjectItemSize },
				{ "item_object",
					Off::InSDK::ObjArray::FUObjectItemInitialOffset },
				{ "chunk_size",
					Off::InSDK::ObjArray::ChunkSize }
			} },
			{ "name_pool", {
				{ "block_offset_bits",
					Off::InSDK::NameArray::FNamePoolBlockOffsetBits },
				{ "entry_stride",
					Off::InSDK::NameArray::FNameEntryStride },
				{ "fname_size",
					Off::InSDK::Name::FNameSize }
			} },
			{ "uobject", {
				{ "flags", Off::UObject::Flags },
				{ "index", Off::UObject::Index },
				{ "class", Off::UObject::Class },
				{ "name", Off::UObject::Name },
				{ "outer", Off::UObject::Outer }
			} },
			{ "ufield", {
				{ "next", Off::UField::Next }
			} },
			{ "ustruct", {
				{ "super", Off::UStruct::SuperStruct },
				{ "children", Off::UStruct::Children },
				{ "child_properties",
					Off::UStruct::ChildProperties },
				{ "size", Off::UStruct::Size },
				{ "min_alignment",
					Off::UStruct::MinAlignment }
			} },
			{ "ffield", {
				{ "class", Off::FField::Class },
				{ "owner", Off::FField::Owner },
				{ "next", Off::FField::Next },
				{ "name", Off::FField::Name }
			} },
			{ "property", {
				{ "array_dim", Off::Property::ArrayDim },
				{ "element_size",
					Off::Property::ElementSize },
				{ "flags",
					Off::Property::PropertyFlags },
				{ "offset_internal",
					Off::Property::Offset_Internal }
			} },
			{ "ufunction", {
				{ "flags",
					Off::UFunction::FunctionFlags },
				{ "native_function",
					Off::UFunction::ExecFunction }
			} },
			{ "insdk", {
				{ "level_actors",
					Off::InSDK::ULevel::Actors },
				{ "datatable_row_map",
					Off::InSDK::UDataTable::RowMap }
			} }
		};
	}
}

std::filesystem::path RuntimeSchema::WriteLocalCache()
{
	/*
	 * FName may use the engine's AppendString routine while Dumper-7 is
	 * running. In that mode the GNames address is discovered but the pool
	 * decoding layout is not committed. The runtime trainer needs both, so
	 * initialize the already-discovered pool solely to derive its validated
	 * stride and block geometry before serializing the schema.
	 */
	if (Off::InSDK::NameArray::GNames
		&& (Off::InSDK::NameArray::FNameEntryStride <= 0
			|| Off::InSDK::NameArray::FNamePoolBlockOffsetBits <= 0)
		&& NameArray::TryInit(
			Off::InSDK::NameArray::GNames,
			true))
	{
		NameArray::PostInit();
	}

	BuildIdentity identity{};
	if (!ReadBuildIdentity(identity))
		return {};

	const std::filesystem::path finalPath =
		GetSchemaPath(identity);
	if (finalPath.empty())
		return {};

	std::error_code error;
	std::filesystem::create_directories(
		finalPath.parent_path(),
		error);
	if (error)
		return {};

	const std::filesystem::path temporaryPath =
		finalPath.wstring() + L".tmp";
	std::ofstream output(
		temporaryPath,
		std::ios::binary | std::ios::trunc);
	if (!output)
		return {};

	const json header{
		{ "format_version", 1 },
		{ "build", {
			{ "time_date_stamp",
				identity.TimeDateStamp },
			{ "size_of_image",
				identity.SizeOfImage },
			{ "file_size",
				identity.FileSize },
			{ "text_sha256",
				identity.TextSha256 },
			{ "fingerprint",
				identity.Fingerprint }
		} },
		{ "core", CoreSchema() }
	};

	std::string serialized = header.dump();
	if (serialized.empty() || serialized.back() != '}')
		return {};
	serialized.pop_back();
	output << serialized << ",\"types\":[";

	bool firstType = true;
	size_t typeCount = 0;
	for (const UEObject object : ObjectArray())
	{
		if (!object
			|| !object.IsA(EClassCastFlags::Struct))
		{
			continue;
		}

		const UEStruct type = object.Cast<UEStruct>();
		const bool isFunction =
			object.IsA(EClassCastFlags::Function);
		const bool isClass =
			object.IsA(EClassCastFlags::Class);
		json entry{
			{ "name", object.GetName() },
			{ "path", object.GetPathName() },
			{ "kind", isFunction
				? "function"
				: isClass
					? "class"
					: "struct" },
			{ "size", type.GetStructSize() },
			{ "alignment", type.GetMinAlignment() },
			{ "properties", json::array() }
		};

		const UEStruct super = type.GetSuper();
		if (super)
			entry["super"] = super.GetPathName();

		if (isClass)
		{
			entry["cast_flags"] =
				static_cast<uint64_t>(
					object.Cast<UEClass>()
						.GetCastFlags());
		}
		if (isFunction)
		{
			entry["function_flags"] =
				static_cast<uint64_t>(
					object.Cast<UEFunction>()
						.GetFunctionFlags());
		}

		for (const UEProperty property :
			type.GetProperties())
		{
			if (property)
				entry["properties"].push_back(
					DescribeProperty(property));
		}

		if (!firstType)
			output << ',';
		firstType = false;
		output << entry.dump();
		++typeCount;
	}
	output << "],\"type_count\":"
		<< typeCount
		<< '}';
	output.flush();
	if (!output)
		return {};
	output.close();

	std::filesystem::remove(finalPath, error);
	error.clear();
	std::filesystem::rename(
		temporaryPath,
		finalPath,
		error);
	if (error)
	{
		std::filesystem::remove(
			temporaryPath,
			error);
		return {};
	}
	return finalPath;
}
