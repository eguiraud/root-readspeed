#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ReadSpeed.hxx"

#include "TFile.h"
#include "TSystem.h"
#include "TTree.h"

using namespace ReadSpeed;

void RequireFile(const std::string &fname)
{
   if (gSystem->AccessPathName(fname.c_str()) == false) // then the file already exists: weird return value convention
      return;                                           // nothing to do

   TFile f(fname.c_str(), "recreate");
   TTree t("t", "t");
   int x = 42;
   t.Branch("x", &x);
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
