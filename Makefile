SRCDIR       =  chp
CXXFLAGS	 =  -O2 -g -Wall -fmessage-length=0 -I../ucs -I../petri -I../arithmetic -I../common -I../interpret_arithmetic -I../interpret_ucs -I../parse_expression -I../parse_ucs -I../parse
SOURCES	    :=  $(shell find $(SRCDIR) -name '*.cpp')
OBJECTS	    :=  $(SOURCES:%.cpp=%.o)
TARGET		 =  lib$(SRCDIR).a

all: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rvs $(TARGET) $(OBJECTS)

%.o: $(SRCDIR)/%.cpp 
	$(CXX) $(CXXFLAGS) -c -o $@ $<
	
clean:
	rm -f $(OBJECTS) $(TARGET)
