/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dataset.h"
#include <cstdio>
#include <cstring>
#include <cuML.hpp>
#include <datasets/make_blobs.hpp>
#include <map>
#include <string>
#include <vector>
#include "argparse.hpp"
#include "utils.h"

namespace ML {
namespace Bench {

void Dataset::allocate(const cumlHandle& handle) {
  auto allocator = handle.getDeviceAllocator();
  auto stream = handle.getStream();
  X = (float*)allocator->allocate(nrows * ncols * sizeof(float), stream);
  y = (int*)allocator->allocate(nrows * sizeof(int), stream);
}

void Dataset::deallocate(const cumlHandle& handle) {
  auto allocator = handle.getDeviceAllocator();
  auto stream = handle.getStream();
  allocator->deallocate(X, nrows * ncols * sizeof(float), stream);
  allocator->deallocate(y, nrows * sizeof(int), stream);
}

void dumpDataset(const cumlHandle& handle, const Dataset& dataset,
                 const std::string& file) {
  printf("Dumping generated dataset to '%s'\n", file.c_str());
  FILE* fp = std::fopen(file.c_str(), "w");
  ASSERT(fp != nullptr, "Failed to open file '%s' for writing", file.c_str());
  auto stream = handle.getStream();
  CUDA_CHECK(cudaStreamSynchronize(stream));
  std::vector<float> X(dataset.nrows * dataset.ncols);
  std::vector<int> y(dataset.nrows);
  MLCommon::updateHost(X.data(), dataset.X, dataset.nrows * dataset.ncols,
                       stream);
  MLCommon::updateHost(y.data(), dataset.y, dataset.nrows, stream);
  CUDA_CHECK(cudaStreamSynchronize(stream));
  fprintf(fp, "%d %d %d\n", dataset.nrows, dataset.ncols, dataset.nclasses);
  for (int i = 0, k = 0; i < dataset.nrows; ++i) {
    for (int j = 0; j < dataset.ncols; ++j, ++k) fprintf(fp, "%f ", X[k]);
    fprintf(fp, "%d\n", y[i]);
  }
  fclose(fp);
}

bool blobs(Dataset& ret, const cumlHandle& handle, int argc, char** argv) {
  bool help = get_argval(argv, argv + argc, "-h");
  if (help) {
    printf(
      "USAGE:\n"
      "bench blobs [options]\n"
      "  Generate a random dataset similar to sklearn's make_blobs.\n"
      "OPTIONS:\n"
      "  -center-box-max <max>   max bounding box for the centers of the\n"
      "                          clusters [10.f].\n"
      "  -center-box-min <min>   min bounding box for the centers of the\n"
      "                          clusters [-10.f].\n"
      "  -cluster-std <std>      cluster std-deviation [1.f].\n"
      "  -dump <file>            dump the generated dataset.\n"
      "  -h                      print this help and exit.\n"
      "  -nclusters <nclusters>  number of clusters to generate [2].\n"
      "  -ncols <ncols>          number of cols in the dataset [81].\n"
      "  -nrows <nrows>          number of rows in the dataset [10001].\n"
      "  -seed <seed>            random seed for reproducibility [1234].\n"
      "  -shuffle                whether to shuffle the dataset.\n");
    return false;
  }
  printf("Generating blobs...\n");
  float centerBoxMax = get_argval(argv, argv + argc, "-center-box-max", 10.f);
  float centerBoxMin = get_argval(argv, argv + argc, "-center-box-min", -10.f);
  float clusterStd = get_argval(argv, argv + argc, "-cluster-std", 1.f);
  std::string dump = get_argval(argv, argv + argc, "-dump", std::string());
  ret.nclasses = get_argval(argv, argv + argc, "-nclusters", 2);
  ret.ncols = get_argval(argv, argv + argc, "-ncols", 81);
  ret.nrows = get_argval(argv, argv + argc, "-nrows", 10001);
  ret.allocate(handle);
  uint64_t seed = get_argval(argv, argv + argc, "-seed", 1234ULL);
  bool shuffle = get_argval(argv, argv + argc, "-shuffle");
  printf(
    "With params:\n"
    "  dimension    = %d,%d\n"
    "  center-box   = %f,%f\n"
    "  cluster-std  = %f\n"
    "  num-clusters = %d\n"
    "  seed         = %lu\n"
    "  shuffle      = %d\n",
    ret.nrows, ret.ncols, centerBoxMin, centerBoxMax, clusterStd, ret.nclasses,
    seed, shuffle);
  Datasets::make_blobs(handle, ret.X, ret.y, ret.nrows, ret.ncols, ret.nclasses,
                       nullptr, nullptr, clusterStd, shuffle, centerBoxMin,
                       centerBoxMax, seed);
  if (dump != "") dumpDataset(handle, ret, dump);
  return true;
}

bool load(Dataset& ret, const cumlHandle& handle, int argc, char** argv) {
  bool help = get_argval(argv, argv + argc, "-h");
  if (help) {
    printf(
      "USAGE:\n"
      "bench load [options]\n"
      "  Load the dataset from the input text file.\n"
      "OPTIONS:\n"
      "  -file <file>   file containing the dataset. Mandatory. File format\n"
      "                 is the same as generated by the '-dump' option.\n"
      "  -h             print this help and exit.\n");
    return false;
  }
  std::string file = get_argval(argv, argv + argc, "-file", std::string());
  ASSERT(!file.empty(), "'-file' is a mandatory option");
  printf("Loading dataset from file '%s'...\n", file.c_str());
  FILE* fp = fopen(file.c_str(), "r");
  ASSERT(fscanf(fp, "%d%d%d", &(ret.nrows), &(ret.ncols), &(ret.nclasses)) == 3,
         "Input dataset file is incorrect! No 'rows cols classes' info found");
  std::vector<float> X(ret.nrows * ret.ncols);
  std::vector<int> y(ret.nrows);
  for (int i = 0, k = 0; i < ret.nrows; ++i) {
    for (int j = 0; j < ret.ncols; ++j, ++k) {
      ASSERT(fscanf(fp, "%f", &(X[k])) == 1,
             "Failed to read input at row,col=%d,%d", i, j);
    }
    ASSERT(fscanf(fp, "%d", &(y[i])) == 1, "Failed to read the label at row=%d",
           i);
  }
  fclose(fp);
  ret.allocate(handle);
  auto stream = handle.getStream();
  MLCommon::copy(ret.X, &(X[0]), ret.nrows * ret.ncols, stream);
  MLCommon::copy(ret.y, &(y[0]), ret.nrows, stream);
  CUDA_CHECK(cudaStreamSynchronize(stream));
  return true;
}

typedef bool (*dataGenerator)(Dataset&, const cumlHandle&, int, char**);
class Generator : public std::map<std::string, dataGenerator> {
 public:
  Generator() : std::map<std::string, dataGenerator>() {
    (*this)["blobs"] = blobs;
    (*this)["load"] = load;
  }
};

/// Do NOT touch anything below this line! ///
/// Only add new loaders above this line ///

Generator& generator() {
  static Generator map;
  return map;
}

std::string allGeneratorNames() {
  const auto& gen = generator();
  std::string ret;
  for (const auto& itr : gen) {
    ret += itr.first + "|";
  }
  ret.pop_back();
  return ret;
}

int findGeneratorStart(int argc, char** argv) {
  const auto& gen = generator();
  for (int i = 0; i < argc; ++i) {
    for (const auto& itr : gen) {
      if (!std::strcmp(itr.first.c_str(), argv[i])) return i;
    }
  }
  return argc;
}

bool loadDataset(Dataset& ret, const cumlHandle& handle, int argc,
                 char** argv) {
  std::string type = argc > 0 ? argv[0] : "blobs";
  auto& gen = generator();
  const auto& itr = gen.find(type);
  ASSERT(itr != gen.end(), "loadDataset: invalid generator name '%s'",
         type.c_str());
  struct timeval start;
  TIC(start);
  auto status = itr->second(ret, handle, argc, argv);
  if (status) {
    printf("dataset dimension: %d x %d\n", ret.nrows, ret.ncols);
    TOC(start, "dataset generation time");
  }
  return status;
}

}  // end namespace Bench
}  // end namespace ML
