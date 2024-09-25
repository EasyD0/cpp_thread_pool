#include <mutex>
#include <thread>
std::mutex m1,m2;
int main(){
	std::scoped_lock L (m1,m2);
	std::lock(m1,m2);
}
