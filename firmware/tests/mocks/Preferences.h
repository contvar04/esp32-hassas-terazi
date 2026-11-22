#ifndef MOCK_PREFERENCES_H
#define MOCK_PREFERENCES_H

#include "Arduino.h"
#include <map>
#include <string>

namespace MockNvsStore {
    extern std::map<std::string, float> floatStore;
    extern bool failBegin;
    extern bool failPut;
    extern int beginCallCount;
    extern int endCallCount;
    extern int putFloatCallCount;
    extern int getFloatCallCount;
    void reset();
}

class Preferences {
private:
    bool _started = false;
    bool _readOnly = false;
public:
    bool begin(const char* name, bool readOnly = false, const char* = nullptr) {
        MockNvsStore::beginCallCount++;
        if (MockNvsStore::failBegin || !name) {
            _started = false;
            return false;
        }
        _started = true;
        _readOnly = readOnly;
        return true;
    }

    void end() {
        if (_started) {
            MockNvsStore::endCallCount++;
            _started = false;
        }
    }

    bool isKey(const char* key) {
        if (!_started || !key) return false;
        return MockNvsStore::floatStore.find(key) != MockNvsStore::floatStore.end();
    }

    float getFloat(const char* key, float defaultValue = NAN) {
        MockNvsStore::getFloatCallCount++;
        if (!_started || !key) return defaultValue;
        auto it = MockNvsStore::floatStore.find(key);
        if (it == MockNvsStore::floatStore.end()) return defaultValue;
        return it->second;
    }

    size_t putFloat(const char* key, float value) {
        MockNvsStore::putFloatCallCount++;
        if (!_started || _readOnly || !key || MockNvsStore::failPut) {
            return 0;
        }
        MockNvsStore::floatStore[key] = value;
        return sizeof(float);
    }
};

#endif // MOCK_PREFERENCES_H
