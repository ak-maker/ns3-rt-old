build_lib(
  LIBNAME sionna_integration

  SOURCE_FILES
    model/sionna-lte-pathloss-model.cc
    helper/sionna-helper.cc
  HEADER_FILES
    model/sionna-lte-pathloss-model.h
    helper/sionna-helper.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libmobility}
    ${liblte}
    # 如果需要别的库就加上
  TEST_SOURCES
    # 如果你有测试/示例可以写在这里
)
