NAME = webserv
CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -I./include

TESTDIR = test
SRCDIR = src
OBJDIR = obj

# Main source files
SRCS = $(SRCDIR)/main.cpp \
       $(SRCDIR)/WebServer.cpp \
       $(SRCDIR)/Config.cpp \
       $(SRCDIR)/ClientConnection.cpp \
       $(SRCDIR)/ConnectionManager.cpp \
       $(SRCDIR)/HttpResponse.cpp

# Request handling files (refactored)
SRCS += $(SRCDIR)/request/HttpRequest.cpp \
        $(SRCDIR)/request/HttpRequestHandlers.cpp \
        $(SRCDIR)/request/HttpRequestHelpers.cpp

# Object files - handle subdirectories
OBJS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

# Pattern rule for main src directory
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(NAME)
	./$(NAME) config/default.conf

subject_test: $(NAME)
	@echo "Running subject test..."
	@./$(NAME) config/subject_test.conf

# Debug build with debugging symbols
debug: CXXFLAGS += -g -DDEBUG
debug: fclean $(NAME)

# Run with debugger
debug_run: debug
	gdb --args ./$(NAME) config/default.conf

# Run automated test suite
test: $(NAME)
	@echo "Running automated tests..."
	$(TESTDIR)/test_server.sh
	$(TESTDIR)/test_multiserver.sh
	$(TESTDIR)/test_config_errors.sh
	$(TESTDIR)/test_uploads.sh

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re run build_test test_config test
