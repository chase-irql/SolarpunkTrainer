// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>

namespace RuntimeSchema
{
	/*
	 * Writes a compact, machine-readable reflection snapshot to the local
	 * per-build cache and returns its final path. An empty path indicates
	 * that generation failed.
	 */
	std::filesystem::path WriteLocalCache();
}
