#include <unordered_map>
#include <string>

namespace Helper {

void ParseText(std::string text, std::unordered_map<std::string, int>& counts);
void StartTimer(const std::string& debugStr = "");
void EndTimer(const std::string& debugStr = "");

} // Helper
