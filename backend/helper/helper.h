#include <vector>
#include <string>

namespace Helper {

void ParseText(std::string text, std::vector<std::string>& words);
void StartTimer(const std::string& debugStr = "");
void EndTimer(const std::string& debugStr = "");

} // Helper
