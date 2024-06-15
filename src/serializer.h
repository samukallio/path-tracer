#pragma once

#include "common.h"

struct scene;

scene* LoadScene(char const* Path);
void SaveScene(char const* Path, scene* Scene);
