NAME = webserv
TEST_CONFIG = test_config

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -I./include

TESTDIR = test
SRCDIR = src
OBJDIR = obj

SRCS = $(SRCDIR)/main.cpp \
       $(SRCDIR)/WebServer.cpp \
       $(SRCDIR)/Config.cpp

OBJS = $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

TEST_SRCS = $(TESTDIR)/test_config.cpp \
            $(SRCDIR)/Config.cpp

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(NAME)
	./$(NAME) config/default.conf

# Build test configuration parser
build_test: $(TEST_CONFIG)

$(TEST_CONFIG): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) $(TEST_SRCS) -o $(TEST_CONFIG)

# Test configuration parser
test_config: build_test
	./$(TEST_CONFIG) config/default.conf

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)
	rm -f $(TEST_CONFIG)

re: fclean all

.PHONY: all clean fclean re run build_test test_config
