/*
 * Copyright (c) 2025, valkey-search contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD 3-Clause
 *
 */

#include "src/indexes/text/snowball_processor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/index_schema.pb.h"
#include "src/indexes/text/language_processor.h"
#include "src/indexes/text/lexer.h"

namespace valkey_search::indexes::text {

// =============================================================================
// Factory tests — verify Create() works for all 12 languages + edge cases
// =============================================================================

class SnowballProcessorFactoryTest
    : public ::testing::TestWithParam<data_model::Language> {
 protected:
  void SetUp() override {
    processor_ = LanguageProcessor::Create(GetParam());
    ASSERT_NE(processor_, nullptr);
  }
  std::shared_ptr<LanguageProcessor> processor_;
};

TEST_P(SnowballProcessorFactoryTest, ReturnsNonNull) {
  EXPECT_NE(processor_, nullptr);
}

TEST_P(SnowballProcessorFactoryTest, SupportsStemming) {
  EXPECT_TRUE(processor_->SupportsStemming());
}

TEST_P(SnowballProcessorFactoryTest, DefaultPunctuationNotEmpty) {
  EXPECT_FALSE(processor_->DefaultPunctuation().empty());
}

INSTANTIATE_TEST_SUITE_P(
    AllLanguages, SnowballProcessorFactoryTest,
    ::testing::Values(data_model::LANGUAGE_ENGLISH,
                      data_model::LANGUAGE_FRENCH,
                      data_model::LANGUAGE_GERMAN,
                      data_model::LANGUAGE_SPANISH,
                      data_model::LANGUAGE_ITALIAN,
                      data_model::LANGUAGE_PORTUGUESE,
                      data_model::LANGUAGE_RUSSIAN,
                      data_model::LANGUAGE_SWEDISH,
                      data_model::LANGUAGE_TURKISH,
                      data_model::LANGUAGE_DUTCH,
                      data_model::LANGUAGE_INDONESIAN,
                      data_model::LANGUAGE_ARABIC));

TEST(SnowballProcessorFactoryTest, UnspecifiedLanguageReturnsNonNull) {
  auto processor = LanguageProcessor::Create(data_model::LANGUAGE_UNSPECIFIED);
  EXPECT_NE(processor, nullptr);
}

// =============================================================================
// Per-language stemming tests (12 languages × 6 tests each)
//
// Each language verifies:
//   1. Basic stemming produces expected output
//   2. A second word stems correctly (different suffix pattern)
//   3. min_stem_size prevents stemming when word is too short
//   4. Already-stemmed word is left unchanged (idempotent)
//   5. BuildStemMap correctly maps multiple variants to shared stem
//   6. BuildStemMap respects min_stem_size (large value → empty map)
//
// French additionally tests multi-byte code point counting to verify
// the AtLeastNCodepoints fix (bytes vs characters).
// =============================================================================

// =============================================================================
// English
// =============================================================================

class SnowballProcessorEnglishTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_ENGLISH);
};

TEST_F(SnowballProcessorEnglishTest, StemWordInPlace_RunningToRun) {
  std::string word = "running";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "run");
}

TEST_F(SnowballProcessorEnglishTest, StemWordInPlace_JumpsToJump) {
  std::string word = "jumps";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "jump");
}

TEST_F(SnowballProcessorEnglishTest, StemWordInPlace_HappilyToHappili) {
  std::string word = "happily";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "happili");
}

TEST_F(SnowballProcessorEnglishTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "running";
  processor_->StemWordInPlace(word, 10);
  EXPECT_EQ(word, "running");
}

TEST_F(SnowballProcessorEnglishTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "run";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "run");
}

TEST_F(SnowballProcessorEnglishTest, BuildStemMap_Basic) {
  std::vector<std::string> tokens = {"running", "jumps", "happily"};
  InProgressStemMap stem_mappings;

  processor_->BuildStemMap(tokens, 3, stem_mappings);

  EXPECT_EQ(stem_mappings.size(), 3);
  EXPECT_TRUE(stem_mappings.contains("run"));
  EXPECT_TRUE(std::find(stem_mappings["run"].begin(),
                        stem_mappings["run"].end(),
                        "running") != stem_mappings["run"].end());
  EXPECT_TRUE(stem_mappings.contains("jump"));
  EXPECT_TRUE(std::find(stem_mappings["jump"].begin(),
                        stem_mappings["jump"].end(),
                        "jumps") != stem_mappings["jump"].end());
  EXPECT_TRUE(stem_mappings.contains("happili"));
  EXPECT_TRUE(std::find(stem_mappings["happili"].begin(),
                        stem_mappings["happili"].end(),
                        "happily") != stem_mappings["happili"].end());
}

TEST_F(SnowballProcessorEnglishTest, BuildStemMap_MultipleWordsToSameStem) {
  std::vector<std::string> tokens = {"running", "runs"};
  InProgressStemMap stem_mappings;

  processor_->BuildStemMap(tokens, 3, stem_mappings);

  EXPECT_EQ(stem_mappings.size(), 1);
  EXPECT_TRUE(stem_mappings.contains("run"));
  EXPECT_EQ(stem_mappings["run"].size(), 2);
  EXPECT_TRUE(std::find(stem_mappings["run"].begin(),
                        stem_mappings["run"].end(),
                        "running") != stem_mappings["run"].end());
  EXPECT_TRUE(std::find(stem_mappings["run"].begin(),
                        stem_mappings["run"].end(),
                        "runs") != stem_mappings["run"].end());
}

TEST_F(SnowballProcessorEnglishTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"running", "jumps", "happily"};
  InProgressStemMap stem_mappings;

  processor_->BuildStemMap(tokens, 100, stem_mappings);

  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// French
// =============================================================================

class SnowballProcessorFrenchTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_FRENCH);
};

TEST_F(SnowballProcessorFrenchTest, StemWordInPlace_Mangeons) {
  std::string word = "mangeons";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "mangeon");
}

TEST_F(SnowballProcessorFrenchTest, StemWordInPlace_Continuellement) {
  std::string word = "continuellement";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "continuel");
}

TEST_F(SnowballProcessorFrenchTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "mangeons";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "mangeons");
}

TEST_F(SnowballProcessorFrenchTest, StemWordInPlace_MultiByteCodePointCount) {
  // "né" is 2 code points but 3 bytes (n + é where é = 0xC3 0xA9)
  // With min_stem_size=3: 2 code points < 3 → should NOT be stemmed
  std::string word = "n\xc3\xa9";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "n\xc3\xa9");
}

TEST_F(SnowballProcessorFrenchTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "mang";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "mang");
}

TEST_F(SnowballProcessorFrenchTest, BuildStemMap_Conjugations) {
  std::vector<std::string> tokens = {"continuellement", "continuelle"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("continuel"));
  EXPECT_EQ(stem_mappings["continuel"].size(), 2);
}

TEST_F(SnowballProcessorFrenchTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"mangeons", "mangeais", "mangez"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// German
// =============================================================================

class SnowballProcessorGermanTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_GERMAN);
};

TEST_F(SnowballProcessorGermanTest, StemWordInPlace_Laufenden) {
  std::string word = "laufenden";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "laufend");
}

TEST_F(SnowballProcessorGermanTest, StemWordInPlace_Aufmerksamkeit) {
  std::string word = "aufmerksamkeit";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "aufmerksam");
}

TEST_F(SnowballProcessorGermanTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "laufenden";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "laufenden");
}

TEST_F(SnowballProcessorGermanTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "laufend";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "laufend");
}

TEST_F(SnowballProcessorGermanTest, BuildStemMap_Variants) {
  std::vector<std::string> tokens = {"laufenden", "laufende"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("laufend"));
  EXPECT_EQ(stem_mappings["laufend"].size(), 2);
}

TEST_F(SnowballProcessorGermanTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"laufenden", "laufende"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Spanish
// =============================================================================

class SnowballProcessorSpanishTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_SPANISH);
};

TEST_F(SnowballProcessorSpanishTest, StemWordInPlace_Corriendo) {
  std::string word = "corriendo";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "corr");
}

TEST_F(SnowballProcessorSpanishTest, StemWordInPlace_Bibliotecas) {
  std::string word = "bibliotecas";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "bibliotec");
}

TEST_F(SnowballProcessorSpanishTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "corriendo";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "corriendo");
}

TEST_F(SnowballProcessorSpanishTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "corr";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "corr");
}

TEST_F(SnowballProcessorSpanishTest, BuildStemMap_Conjugations) {
  std::vector<std::string> tokens = {"corriendo", "corremos"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("corr"));
  EXPECT_EQ(stem_mappings["corr"].size(), 2);
}

TEST_F(SnowballProcessorSpanishTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"corriendo", "corremos"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Italian
// =============================================================================

class SnowballProcessorItalianTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_ITALIAN);
};

TEST_F(SnowballProcessorItalianTest, StemWordInPlace_Mangiando) {
  std::string word = "mangiando";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "mang");
}

TEST_F(SnowballProcessorItalianTest, StemWordInPlace_Continuamente) {
  std::string word = "continuamente";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "continu");
}

TEST_F(SnowballProcessorItalianTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "mangiando";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "mangiando");
}

TEST_F(SnowballProcessorItalianTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "mang";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "mang");
}

TEST_F(SnowballProcessorItalianTest, BuildStemMap_Conjugations) {
  std::vector<std::string> tokens = {"mangiando", "mangiamo"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("mang"));
  EXPECT_EQ(stem_mappings["mang"].size(), 2);
}

TEST_F(SnowballProcessorItalianTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"mangiando", "mangiamo"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Portuguese
// =============================================================================

class SnowballProcessorPortugueseTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_PORTUGUESE);
};

TEST_F(SnowballProcessorPortugueseTest, StemWordInPlace_Correndo) {
  std::string word = "correndo";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "corr");
}

TEST_F(SnowballProcessorPortugueseTest, StemWordInPlace_Bibliotecas) {
  std::string word = "bibliotecas";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "bibliotec");
}

TEST_F(SnowballProcessorPortugueseTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "correndo";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "correndo");
}

TEST_F(SnowballProcessorPortugueseTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "corr";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "corr");
}

TEST_F(SnowballProcessorPortugueseTest, BuildStemMap_Conjugations) {
  std::vector<std::string> tokens = {"correndo", "corremos"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("corr"));
  EXPECT_EQ(stem_mappings["corr"].size(), 2);
}

TEST_F(SnowballProcessorPortugueseTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"correndo", "corremos"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Russian
// =============================================================================

class SnowballProcessorRussianTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_RUSSIAN);
};

TEST_F(SnowballProcessorRussianTest, StemWordInPlace_Running) {
  // бегущий (running) -> бегущ
  std::string word = "\xd0\xb1\xd0\xb5\xd0\xb3\xd1\x83\xd1\x89\xd0\xb8\xd0\xb9";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "\xd0\xb1\xd0\xb5\xd0\xb3\xd1\x83\xd1\x89");
}

TEST_F(SnowballProcessorRussianTest, StemWordInPlace_Libraries) {
  // библиотеки (libraries) -> библиотек
  std::string word = "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba\xd0\xb8";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba");
}

TEST_F(SnowballProcessorRussianTest, StemWordInPlace_MinStemSizePrevents) {
  // бегущий — should remain unchanged with large min_stem_size
  std::string word = "\xd0\xb1\xd0\xb5\xd0\xb3\xd1\x83\xd1\x89\xd0\xb8\xd0\xb9";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "\xd0\xb1\xd0\xb5\xd0\xb3\xd1\x83\xd1\x89\xd0\xb8\xd0\xb9");
}

TEST_F(SnowballProcessorRussianTest, StemWordInPlace_AlreadyStemmed) {
  // библиотек (already the stem)
  std::string word = "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba");
}

TEST_F(SnowballProcessorRussianTest, BuildStemMap_CaseVariants) {
  // библиотеки, библиотека -> библиотек
  std::vector<std::string> tokens = {
      "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba\xd0\xb8",
      "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba\xd0\xb0"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains(
      "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba"));
  EXPECT_EQ(stem_mappings["\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba"].size(), 2);
}

TEST_F(SnowballProcessorRussianTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {
      "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba\xd0\xb8",
      "\xd0\xb1\xd0\xb8\xd0\xb1\xd0\xbb\xd0\xb8\xd0\xbe\xd1\x82\xd0\xb5\xd0\xba\xd0\xb0"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Swedish
// =============================================================================

class SnowballProcessorSwedishTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_SWEDISH);
};

TEST_F(SnowballProcessorSwedishTest, StemWordInPlace_Springande) {
  std::string word = "springande";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "spring");
}

TEST_F(SnowballProcessorSwedishTest, StemWordInPlace_Flickorna) {
  std::string word = "flickorna";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "flick");
}

TEST_F(SnowballProcessorSwedishTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "springande";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "springande");
}

TEST_F(SnowballProcessorSwedishTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "spring";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "spring");
}

TEST_F(SnowballProcessorSwedishTest, BuildStemMap_Variants) {
  std::vector<std::string> tokens = {"springande", "springer"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("spring"));
  EXPECT_EQ(stem_mappings["spring"].size(), 2);
}

TEST_F(SnowballProcessorSwedishTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"springande", "springer"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Turkish
// =============================================================================

class SnowballProcessorTurkishTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_TURKISH);
};

TEST_F(SnowballProcessorTurkishTest, StemWordInPlace_Kosuyorlar) {
  // koşuyorlar (they are running) -> koşuyor
  std::string word = "ko\xc5\x9fuyorlar";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "ko\xc5\x9fuyor");
}

TEST_F(SnowballProcessorTurkishTest, StemWordInPlace_Evlerden) {
  std::string word = "evlerden";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "ev");
}

TEST_F(SnowballProcessorTurkishTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "evlerden";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "evlerden");
}

TEST_F(SnowballProcessorTurkishTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "ev";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "ev");
}

TEST_F(SnowballProcessorTurkishTest, BuildStemMap_Suffixes) {
  std::vector<std::string> tokens = {"evlerden", "evlerde"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("ev"));
  EXPECT_EQ(stem_mappings["ev"].size(), 2);
}

TEST_F(SnowballProcessorTurkishTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"evlerden", "evlerde"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Dutch
// =============================================================================

class SnowballProcessorDutchTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_DUTCH);
};

TEST_F(SnowballProcessorDutchTest, StemWordInPlace_Lopende) {
  std::string word = "lopende";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "loop");
}

TEST_F(SnowballProcessorDutchTest, StemWordInPlace_Fietsen) {
  std::string word = "fietsen";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "fiets");
}

TEST_F(SnowballProcessorDutchTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "lopende";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "lopende");
}

TEST_F(SnowballProcessorDutchTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "loop";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "loop");
}

TEST_F(SnowballProcessorDutchTest, BuildStemMap_Variants) {
  std::vector<std::string> tokens = {"lopende", "lopend"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("loop"));
  EXPECT_EQ(stem_mappings["loop"].size(), 2);
}

TEST_F(SnowballProcessorDutchTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"lopende", "lopend"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Indonesian
// =============================================================================

class SnowballProcessorIndonesianTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_INDONESIAN);
};

TEST_F(SnowballProcessorIndonesianTest, StemWordInPlace_Berlari) {
  std::string word = "berlari";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "lari");
}

TEST_F(SnowballProcessorIndonesianTest, StemWordInPlace_Mempermasalahkan) {
  std::string word = "mempermasalahkan";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "masalah");
}

TEST_F(SnowballProcessorIndonesianTest, StemWordInPlace_MinStemSizePrevents) {
  std::string word = "berlari";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "berlari");
}

TEST_F(SnowballProcessorIndonesianTest, StemWordInPlace_AlreadyStemmed) {
  std::string word = "lari";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "lari");
}

TEST_F(SnowballProcessorIndonesianTest, BuildStemMap_Prefixes) {
  std::vector<std::string> tokens = {"berlari", "pelari"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_TRUE(stem_mappings.contains("lari"));
  EXPECT_EQ(stem_mappings["lari"].size(), 2);
}

TEST_F(SnowballProcessorIndonesianTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {"berlari", "pelari"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

// =============================================================================
// Arabic
// =============================================================================

class SnowballProcessorArabicTest : public ::testing::Test {
 protected:
  std::shared_ptr<LanguageProcessor> processor_ =
      LanguageProcessor::Create(data_model::LANGUAGE_ARABIC);
};

TEST_F(SnowballProcessorArabicTest, StemWordInPlace_Books) {
  // الكتب (the books) -> كتب
  std::string word = "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa8";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "\xd9\x83\xd8\xaa\xd8\xa8");
}

TEST_F(SnowballProcessorArabicTest, StemWordInPlace_Schools) {
  // المدارس (the schools) -> مدارس
  std::string word = "\xd8\xa7\xd9\x84\xd9\x85\xd8\xaf\xd8\xa7\xd8\xb1\xd8\xb3";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "\xd9\x85\xd8\xaf\xd8\xa7\xd8\xb1\xd8\xb3");
}

TEST_F(SnowballProcessorArabicTest, StemWordInPlace_MinStemSizePrevents) {
  // الكتب — should remain unchanged with large min_stem_size
  std::string word = "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa8";
  processor_->StemWordInPlace(word, 100);
  EXPECT_EQ(word, "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa8");
}

TEST_F(SnowballProcessorArabicTest, StemWordInPlace_AlreadyStemmed) {
  // كتب (already the stem of الكتب)
  std::string word = "\xd9\x83\xd8\xaa\xd8\xa8";
  processor_->StemWordInPlace(word, 3);
  EXPECT_EQ(word, "\xd9\x83\xd8\xaa\xd8\xa8");
}

TEST_F(SnowballProcessorArabicTest, BuildStemMap_DefiniteArticle) {
  // الكتب (the books), الكتاب (the book) -> both strip ال prefix
  std::vector<std::string> tokens = {
      "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa8",
      "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa7\xd8\xa8"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 3, stem_mappings);
  EXPECT_EQ(stem_mappings.size(), 2);
}

TEST_F(SnowballProcessorArabicTest, BuildStemMap_MinStemSizePreventsAll) {
  std::vector<std::string> tokens = {
      "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa8",
      "\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa7\xd8\xa8"};
  InProgressStemMap stem_mappings;
  processor_->BuildStemMap(tokens, 100, stem_mappings);
  EXPECT_TRUE(stem_mappings.empty());
}

}  // namespace valkey_search::indexes::text
