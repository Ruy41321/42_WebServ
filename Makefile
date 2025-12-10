NAME = webserv
CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -I./include

TESTDIR = test
SRCDIR = src
OBJDIR = obj
REDIRECT_LOG_FILE = /tmp/webserver_log.txt
REDIRECT_STATE_FILE = .redirect_enabled

# Main source files
SRCS = $(SRCDIR)/main.cpp \
       $(SRCDIR)/WebServer.cpp \
       $(SRCDIR)/Config.cpp \
       $(SRCDIR)/ClientConnection.cpp \
       $(SRCDIR)/ConnectionManager.cpp \
       $(SRCDIR)/HttpResponse.cpp \
       $(SRCDIR)/CgiHandler.cpp

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
	@if [ -f $(REDIRECT_STATE_FILE) ]; then \
		echo "Running with output redirected to $(REDIRECT_LOG_FILE)"; \
		./$(NAME) config/default.conf >> $(REDIRECT_LOG_FILE) 2>&1; \
	else \
		./$(NAME) config/default.conf; \
	fi

toggle_redirection:
	@if [ -f $(REDIRECT_STATE_FILE) ]; then \
		rm $(REDIRECT_STATE_FILE); \
		echo "Redirection DISABLED - next 'make run' will output to terminal"; \
	else \
		touch $(REDIRECT_STATE_FILE); \
		echo "Redirection ENABLED - next 'make run' will output to $(REDIRECT_LOG_FILE)"; \
	fi
	@echo "Current state: $$([ -f $(REDIRECT_STATE_FILE) ] && echo 'ENABLED' || echo 'DISABLED')"

subject_test: $(NAME)
	@echo "Checking if server is running on port 8084..."
	@if curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8084 | grep -q "200\|404\|405\|500"; then \
		echo "Running subject test..."; \
		./subject/ubuntu_tester http://127.0.0.1:8084; \
	else \
		echo ""; \
		echo "ERROR: Server is not running on http://127.0.0.1:8084"; \
		echo ""; \
		echo "Please start the server first with:"; \
		echo "  make run"; \
		echo ""; \
		exit 1; \
	fi

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
	$(TESTDIR)/test_http11.sh
	$(TESTDIR)/test_body_size_limit.sh
	$(TESTDIR)/test_multiserver.sh
	$(TESTDIR)/test_config_errors.sh
	$(TESTDIR)/test_uploads.sh
	$(TESTDIR)/test_cgi.sh

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all


.PHONY: all clean fclean re run build_test test_config test
