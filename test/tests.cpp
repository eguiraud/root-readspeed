/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ReadSpeed.hxx"
#include "ReadSpeedCLI.hxx"

#include "TFile.h"
#include "TSystem.h"
#include "TTree.h"

using namespace ReadSpeed;

void RequireFile(const std::string &fname, const std::vector<std::string> &branchNames = {"x"})
{
   if (gSystem->AccessPathName(fname.c_str()) == false) // then the file already exists: weird return value convention
      return;                                           // nothing to do

   TFile f(fname.c_str(), "recreate");
   TTree t("t", "t");

   int var = 42;
   for (const auto &b : branchNames) {
      t.Branch(b.c_str(), &var);
   }

   for (int i = 0; i < 10000000; ++i)
      t.Fill();
   t.Write();
}

std::vector<std::string> ConcatVectors(const std::vector<std::string> &first, const std::vector<std::string> &second)
{
   std::vector<std::string> all;

   all.insert(all.end(), first.begin(), first.end());
   all.insert(all.end(), second.begin(), second.end());

   return all;
}

TEST_CASE("Integration test")
{
   RequireFile("test1.root");
   RequireFile("test2.root");

   SUBCASE("Single-thread run")
   {
      const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 80000000, "Wrong number of bytes read");
      CHECK_MESSAGE(result.fCompressedBytesRead == 643934, "Wrong number of compressed bytes read");
   }
   SUBCASE("Multi-thread run")
   {
      const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 2);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 80000000, "Wrong number of bytes read");
      CHECK_MESSAGE(result.fCompressedBytesRead == 643934, "Wrong number of compressed bytes read");
   }
   SUBCASE("Invalid filename")
   {
      CHECK_THROWS_WITH(EvalThroughput({{"t"}, {"test_fake.root"}, {"x"}}, 0), "Could not open file 'test_fake.root'");
   }
   SUBCASE("Invalid tree")
   {
      CHECK_THROWS_WITH(EvalThroughput({{"t_fake"}, {"test1.root"}, {"x"}}, 0),
                        "Could not retrieve tree 't_fake' from file 'test1.root'");
   }
   SUBCASE("Invalid branch")
   {
      CHECK_THROWS_WITH(EvalThroughput({{"t"}, {"test1.root"}, {"z"}}, 0),
                        "Could not retrieve branch 'z' from tree 't' in file 'test1.root'");
   }

   gSystem->Unlink("test1.root");
   gSystem->Unlink("test2.root");
}

TEST_CASE("Branch test")
{
   RequireFile("test3.root", {"x", "x_branch", "y_brunch", "mismatched"});

   SUBCASE("Single branch")
   {
      const auto result = EvalThroughput({{"t"}, {"test3.root"}, {"x"}}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 40000000, "Wrong number of uncompressed bytes read");
      CHECK_MESSAGE(result.fCompressedBytesRead == 321967, "Wrong number of compressed bytes read");
   }
   SUBCASE("Pattern branches")
   {
      const auto result = EvalThroughput({{"t"}, {"test3.root"}, {"(x|y)_.*nch"}, true}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 80000000, "Wrong number of uncompressed bytes read");
      CHECK_MESSAGE(result.fCompressedBytesRead == 661576, "Wrong number of compressed bytes read");
   }
   SUBCASE("No matches")
   {
      CHECK_THROWS(EvalThroughput({{"t"}, {"test3.root"}, {"x_.*"}, false}, 0));
      CHECK_THROWS(EvalThroughput({{"t"}, {"test3.root"}, {"z_.*"}, true}, 0));
   }
   SUBCASE("All branches")
   {
      const auto result = EvalThroughput({{"t"}, {"test3.root"}, {".*"}, true}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 160000000, "Wrong number of uncompressed bytes read");
      CHECK_MESSAGE(result.fCompressedBytesRead == 1316837, "Wrong number of compressed bytes read");
   }

   gSystem->Unlink("test3.root");
}

TEST_CASE("CLI test")
{
   SUBCASE("Filename list")
   {
      const std::vector<std::string> baseArgs{"root-readspeed", "--trees", "t", "--branches", "x", "--files"};
      const std::vector<std::string> inFiles{"file-a.root", "file-b.root", "file-c.root"};

      const auto allArgs = ConcatVectors(baseArgs, inFiles);

      const auto parsedArgs = ParseArgs(allArgs);
      const auto outFiles = parsedArgs.fData.fFileNames;

      CHECK_MESSAGE(outFiles.size() == inFiles.size(),
                    "Number of parsed files does not match number of provided files.");
      CHECK_MESSAGE(outFiles == inFiles, "List of parsed files does not match list of provided files.");
   }
   SUBCASE("Tree list")
   {
      const std::vector<std::string> baseArgs{"root-readspeed", "--files", "file.root", "--branches", "x", "--trees"};
      const std::vector<std::string> inTrees{"t1", "t2", "tree3"};

      const auto allArgs = ConcatVectors(baseArgs, inTrees);

      const auto parsedArgs = ParseArgs(allArgs);
      const auto outTrees = parsedArgs.fData.fTreeNames;

      CHECK_MESSAGE(outTrees.size() == inTrees.size(),
                    "Number of parsed trees does not match number of provided trees.");
      CHECK_MESSAGE(outTrees == inTrees, "List of parsed trees does not match list of provided trees.");
   }
   SUBCASE("Branch list")
   {
      const std::vector<std::string> baseArgs{
         "root-readspeed", "--files", "file.root", "--trees", "t", "--branches",
      };
      const std::vector<std::string> inBranches{"x", "x_branch", "long_branch_name"};

      const auto allArgs = ConcatVectors(baseArgs, inBranches);

      const auto parsedArgs = ParseArgs(allArgs);
      const auto outBranches = parsedArgs.fData.fBranchNames;

      CHECK_MESSAGE(outBranches.size() == inBranches.size(),
                    "Number of parsed trees does not match number of provided trees.");
      CHECK_MESSAGE(outBranches == inBranches, "List of parsed trees does not match list of provided trees.");
   }
}