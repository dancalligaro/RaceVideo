#include "common/input_list.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "gtest/gtest.h"

namespace racevideo {
namespace {

TEST(ReadInputListTest, PreservesOrderAndResolvesRelativePaths) {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "racevideo_input_list_test";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  ASSERT_FALSE(error);
  const std::filesystem::path list = directory / "chapters.txt";
  {
    std::ofstream output(list);
    output << " first.mp4 \n\nsub/second.mp4\n";
  }

  const auto paths = ReadInputList(list);

  ASSERT_TRUE(paths.ok()) << paths.status();
  ASSERT_EQ(paths->size(), 2);
  EXPECT_EQ((*paths)[0], directory / "first.mp4");
  EXPECT_EQ((*paths)[1], directory / "sub" / "second.mp4");
  std::filesystem::remove_all(directory, error);
  EXPECT_FALSE(error);
}

}  // namespace
}  // namespace racevideo
