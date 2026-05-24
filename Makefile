CXX = g++
CXXFLAGS = -std=c++11 -Wall -g -pthread
INCLUDES = -I./include

TARGET = server
SRCDIR = src
OBJDIR = obj

SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

run: $(TARGET)
	./$(TARGET) server.yaml

test: $(TARGET)
	./$(TARGET) server.yaml &
	@echo "Server started, testing APIs..."
	@sleep 1
	@curl -s http://localhost:8080/stats | head -5
	@echo ""
	@curl -s http://localhost:8080/hello?name=TinyWeb
	@echo ""
	@curl -s http://localhost:8080/time
	@echo ""
	@curl -s http://localhost:8080/
	@echo ""
	@fuser -k 8080/tcp 2>/dev/null || true
