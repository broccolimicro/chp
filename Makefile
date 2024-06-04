NAME          = chp
DEPEND        = ucs petri arithmetic common interpret_arithmetic interpret_ucs parse_expression parse_ucs parse

SRCDIR        = $(NAME)
TESTDIR       = tests
GTEST        := ../../googletest
GTEST_I      := -I$(GTEST)/googletest/include -I.
GTEST_L      := -L$(GTEST)/build/lib -L.

CXXFLAGS	    = -O2 -g -Wall -fmessage-length=0 $(DEPEND:%=-I../%) -I.
LDFLAGS		    =  

SOURCES	     := $(shell find $(SRCDIR) -name '*.cpp')
OBJECTS	     := $(SOURCES:%.cpp=%.o)
DEPS         := $(OBJECTS:%.o=%.d)
TARGET		    = lib$(NAME).a

TESTS        := $(shell find $(TESTDIR) -name '*.cpp')
TEST_OBJECTS := $(TESTS:%.cpp=%.o) $(TESTDIR)/gtest_main.o
TEST_DEPS    := $(TEST_OBJECTS:%.o=%.d)
TEST_TARGET   = test

-include $(DEPS)
-include $(TEST_DEPS)

all: lib

lib: $(TARGET)

tests: lib $(TEST_TARGET)

$(TARGET): $(OBJECTS)
	ar rvs $(TARGET) $(OBJECTS)

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -MM -MF $(patsubst %.o,%.d,$@) -MT $@ -c $<
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -c -o $@ $<

$(TEST_TARGET): $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(GTEST_L) $^ -pthread -l$(NAME) -lgtest -o $(TEST_TARGET)

$(TESTDIR)/%.o: $(TESTDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(GTEST_I) -MM -MF $(patsubst %.o,%.d,$@) -MT $@ -c $<
	$(CXX) $(CXXFLAGS) $(GTEST_I) $< -c -o $@

$(TESTDIR)/gtest_main.o: $(GTEST)/googletest/src/gtest_main.cc
	$(CXX) $(CXXFLAGS) $(GTEST_I) $< -c -o $@

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)
	rm -f $(TEST_OBJECTS) $(TEST_DEPS) $(TEST_TARGET)

