cmake_minimum_required(VERSION 3.16)

# This file is usable both as a standalone project (for fast core tests) and as
# a subdirectory of the chiaki-ng fork (add_subdirectory(translator_core)).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(TRANS_STANDALONE ON)
	project(chiaki_translator_core LANGUAGES CXX)
else()
	set(TRANS_STANDALONE OFF)
endif()

option(TRANS_BUILD_TESTS "Build translator_core unit tests" ${TRANS_STANDALONE})
option(TRANS_ENABLE_ONNXRUNTIME "Link ONNX Runtime and enable the OCR backend" OFF)
option(TRANS_ENABLE_CTRANSLATE2 "Link CTranslate2 and enable the offline NLLB backend" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Prefix containing include/onnxruntime and lib/libonnxruntime")

if(TRANS_STANDALONE)
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
endif()

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
if(TARGET SQLite3::SQLite3)
	set(TRANS_SQLITE_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
	set(TRANS_SQLITE_TARGET SQLite::SQLite3)
else()
	set(TRANS_SQLITE_TARGET ${SQLite3_LIBRARIES})
endif()

add_library(translator_core STATIC
	src/FrameProcessor.cpp
	src/TextProcessor.cpp
	src/OcrPostprocess.cpp
	src/OcrEngine.cpp
	src/OnnxOcrEngine.cpp
	src/DictLookup.cpp
	src/TranslatorBackend.cpp
	src/TranslatorPipeline.cpp
	src/NllbBackend.cpp
)
target_include_directories(translator_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(translator_core PUBLIC cxx_std_17)
set_target_properties(translator_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(translator_core PUBLIC ${TRANS_SQLITE_TARGET} Threads::Threads)

if(TRANS_ENABLE_ONNXRUNTIME)
	find_path(TRANS_ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
		HINTS
			${ONNXRUNTIME_ROOT}/include/onnxruntime
			$ENV{ONNXRUNTIME_ROOT}/include/onnxruntime
			/opt/homebrew/include/onnxruntime
			/usr/include/onnxruntime
			/usr/local/include/onnxruntime)
	find_library(TRANS_ONNXRUNTIME_LIBRARY onnxruntime
		HINTS
			${ONNXRUNTIME_ROOT}/lib
			$ENV{ONNXRUNTIME_ROOT}/lib
			/opt/homebrew/lib
			/usr/lib
			/usr/local/lib)
	if(NOT TRANS_ONNXRUNTIME_INCLUDE_DIR OR NOT TRANS_ONNXRUNTIME_LIBRARY)
		message(FATAL_ERROR "ONNX Runtime not found; set ONNXRUNTIME_ROOT or turn TRANS_ENABLE_ONNXRUNTIME off")
	endif()
	target_include_directories(translator_core PUBLIC ${TRANS_ONNXRUNTIME_INCLUDE_DIR})
	target_compile_definitions(translator_core PUBLIC TRANS_HAVE_ONNXRUNTIME=1)
	target_link_libraries(translator_core PUBLIC ${TRANS_ONNXRUNTIME_LIBRARY})
endif()

if(TRANS_ENABLE_CTRANSLATE2)
	find_path(TRANS_CTRANSLATE2_INCLUDE_DIR ctranslate2/translator.h
		HINTS
			${CTRANSLATE2_ROOT}/include
			$ENV{CTRANSLATE2_ROOT}/include
			/opt/homebrew/include
			/usr/local/include
			/usr/include)
	find_library(TRANS_CTRANSLATE2_LIBRARY NAMES ctranslate2
		HINTS
			${CTRANSLATE2_ROOT}/lib
			$ENV{CTRANSLATE2_ROOT}/lib
			/opt/homebrew/lib
			/usr/local/lib
			/usr/lib)
	find_path(TRANS_SENTENCEPIECE_INCLUDE_DIR sentencepiece_processor.h
		HINTS
			${SENTENCEPIECE_ROOT}/include
			$ENV{SENTENCEPIECE_ROOT}/include
			/opt/homebrew/include
			/usr/local/include
			/usr/include)
	find_library(TRANS_SENTENCEPIECE_LIBRARY NAMES sentencepiece
		HINTS
			${SENTENCEPIECE_ROOT}/lib
			$ENV{SENTENCEPIECE_ROOT}/lib
			/opt/homebrew/lib
			/usr/local/lib
			/usr/lib)
	if(NOT TRANS_CTRANSLATE2_INCLUDE_DIR OR NOT TRANS_CTRANSLATE2_LIBRARY OR
			NOT TRANS_SENTENCEPIECE_INCLUDE_DIR OR NOT TRANS_SENTENCEPIECE_LIBRARY)
		message(FATAL_ERROR "CTranslate2/SentencePiece not found; set CTRANSLATE2_ROOT/SENTENCEPIECE_ROOT or turn TRANS_ENABLE_CTRANSLATE2 off")
	endif()
	target_include_directories(translator_core PUBLIC
		${TRANS_CTRANSLATE2_INCLUDE_DIR} ${TRANS_SENTENCEPIECE_INCLUDE_DIR})
	target_compile_definitions(translator_core PUBLIC TRANS_HAVE_CTRANSLATE2=1)
	target_link_libraries(translator_core PUBLIC
		${TRANS_CTRANSLATE2_LIBRARY} ${TRANS_SENTENCEPIECE_LIBRARY})
	# SentencePiece's public Status API pulls in Abseil on recent builds.
	find_package(absl CONFIG QUIET)
	if(TARGET absl::status)
		target_link_libraries(translator_core PUBLIC absl::status)
	endif()
endif()

if(TRANS_BUILD_TESTS)
	enable_testing()
	add_executable(translator_tests
		tests/TestMain.cpp
		tests/TestFrame.cpp
		tests/TestText.cpp
		tests/TestOcr.cpp
		tests/TestDict.cpp
		tests/TestBackend.cpp
		tests/TestPipeline.cpp
	)
	target_link_libraries(translator_tests PRIVATE translator_core)
	set_target_properties(translator_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	target_compile_options(translator_tests PRIVATE -Wall -Wextra -Wpedantic)
	add_test(NAME translator_tests COMMAND translator_tests)
endif()

add_executable(translator_dict_probe
	tools/dict_probe.cpp
)
target_link_libraries(translator_dict_probe PRIVATE translator_core)

if(TRANS_ENABLE_CTRANSLATE2)
	add_executable(translator_nllb_probe
		tools/nllb_probe.cpp
	)
	target_link_libraries(translator_nllb_probe PRIVATE translator_core)
endif()

if(TRANS_ENABLE_ONNXRUNTIME)
	add_executable(translator_onnx_tests
		tests/TestMain.cpp
		tests/TestOnnx.cpp
	)
	target_link_libraries(translator_onnx_tests PRIVATE translator_core)
	target_compile_definitions(translator_onnx_tests PRIVATE
		TRANS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
		TRANS_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
	add_test(NAME translator_onnx_tests COMMAND translator_onnx_tests)

	add_executable(translator_ocr_probe
		tools/ocr_probe.cpp
	)
	target_link_libraries(translator_ocr_probe PRIVATE translator_core)
endif()
