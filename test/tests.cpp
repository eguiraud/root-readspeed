#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ReadSpeed.hxx"

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

TEST_CASE("Integration test")
{
   RequireFile("test1.root");
   RequireFile("test2.root");

   SUBCASE("Single-thread run")
   {
      const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 80000000, "Wrong number of bytes read");
   }
   SUBCASE("Multi-thread run")
   {
      const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 2);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 80000000, "Wrong number of bytes read");
   }

   gSystem->Unlink("test1.root");
   gSystem->Unlink("test2.root");
}

TEST_CASE("Branch test")
{
   RequireFile("test3.root", { "x", "x_branch", "y_brunch", "mismatched" });

   SUBCASE("Single branch")
   {
      const auto result = EvalThroughput({{"t"}, {"test3.root"}, {"x"}}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 40000000, "Wrong number of bytes read");
   }
   SUBCASE("Pattern branches")
   {
      const auto result = EvalThroughput({{"t"}, {"test3.root"}, {"(x|y)_.*nch"}, true}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 80000000, "Wrong number of bytes read");
   }
   SUBCASE("No matches")
   {
      CHECK_THROWS(EvalThroughput({{"t"}, {"test3.root"}, {"x_.*"}, false}, 0));
      CHECK_THROWS(EvalThroughput({{"t"}, {"test3.root"}, {"z_.*"}, true}, 0));
   }
   SUBCASE("All branches")
   {
      const auto result = EvalThroughput({{"t"}, {"test3.root"}, {".*"}, true}, 0);
      CHECK_MESSAGE(result.fUncompressedBytesRead == 160000000, "Wrong number of bytes read");
   }
   
   gSystem->Unlink("test3.root");
}