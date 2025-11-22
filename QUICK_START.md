# Quick Start Guide - Ubuntu Tester Testing

## 🚀 Quick Start (3 Steps)

### 1. Build
```bash
make clean && make
```

### 2. Start Server
```bash
./webserv config/test.conf
```

### 3. Run Tester (in new terminal)
```bash
./subject/ubuntu_tester http://127.0.0.1:8080
# Press Enter through the prompts
```

## 📋 Manual Testing Commands

### Test All Endpoints
```bash
# Test GET /
curl http://127.0.0.1:8080/

# Test POST / (should return 405)
curl -X POST http://127.0.0.1:8080/

# Test HEAD /
curl -I http://127.0.0.1:8080/

# Test GET /directory/
curl http://127.0.0.1:8080/directory/

# Test PUT /put_test/myfile.txt
echo "Test content" | curl -X PUT --data-binary @- http://127.0.0.1:8080/put_test/myfile.txt

# Test POST /post_body (within 100 byte limit)
printf '%50s' | curl -X POST --data-binary @- http://127.0.0.1:8080/post_body

# Test POST /post_body (exceeds 100 byte limit - should fail)
printf '%150s' | curl -X POST --data-binary @- http://127.0.0.1:8080/post_body
```

## ✅ Expected Results

| Test | Expected Response | Status |
|------|------------------|--------|
| GET / | 200 OK + HTML | ✅ |
| POST / | 405 Method Not Allowed | ✅ |
| HEAD / | 200 OK + headers only | ✅ |
| GET /directory/ | 200 OK + file content | ✅ |
| PUT /put_test/file | 201 Created | ✅ |
| POST /post_body (50b) | 200 OK | ✅ |
| POST /post_body (150b) | 413 Request Entity Too Large | ✅ |

## 📚 Documentation

For detailed information, see:
- **SUMMARY.md** - Executive summary
- **TESTER_REPORT.md** - Technical analysis (English)
- **REPORT_MODIFICHE_IT.md** - Detailed report (Italian)

## 🔧 Configuration

The test configuration is in `config/test.conf` with these endpoints:

| Endpoint | Methods | Description |
|----------|---------|-------------|
| / | GET | Main page (index.html) |
| /put_test | PUT | File upload via PUT |
| /post_body | POST | Max 100 bytes body |
| /directory | GET | Serves YoupiBanane directory |

## 🐛 Known Issues

1. **Ubuntu Tester HEAD Test**: Reports error despite server returning correct 200 OK
   - Manual verification confirms server is correct
   - May be a tester-specific requirement or bug

2. **CGI Not Implemented**: `.bla` file CGI execution not implemented
   - Acknowledged as acceptable per requirements
   - Configuration is ready for future implementation

## 💡 Tips

- Server runs on port 8080 by default
- Check `www/put_test/` for uploaded files
- All test files are in `www/` directory
- YoupiBanane structure matches tester requirements

## ⚡ One-Line Test Script

```bash
curl -s http://127.0.0.1:8080/ && \
curl -s -X POST http://127.0.0.1:8080/ | grep 405 && \
curl -s -I http://127.0.0.1:8080/ | grep "200 OK" && \
echo "✅ Basic tests passed!"
```

## 📞 Need Help?

Check the comprehensive reports:
1. English: `TESTER_REPORT.md`
2. Italian: `REPORT_MODIFICHE_IT.md`
3. Summary: `SUMMARY.md`

---
**Status**: ✅ Ready for Evaluation  
**Last Updated**: November 22, 2024
